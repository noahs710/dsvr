"""Classify what happened inside each declared case window, post-hoc.

Reads combat-cases.json (QPC windows) + a DSVR trace, reports per window:
contacts, HP deltas by side, stamina-only (block-signature) events,
kind histogram from diagnostics, identity transitions, interactions.
No game, no native calls. Outcome labels are observed distinctions
(hit vs block signatures), never damage attribution.
"""
import json
import sys
from pathlib import Path


def load_cases(path):
    with open(path) as f:
        plan = json.load(f)
    return [(c["label"], c["start_qpc"], c["end_qpc"], c.get("state"))
            for c in plan["cases"] if c.get("end_qpc") is not None]


def main(trace_path, cases_path):
    windows = load_cases(cases_path)
    events = []
    with open(trace_path) as f:
        for line in f:
            try:
                d = json.loads(line)
            except Exception:
                continue
            events.append(d)
    hdr = next((e["qpc"] for e in events if e.get("event") == "header"), 0)

    def t(q):
        return (q - hdr) / 1e7

    for label, start, end, state in windows:
        in_win = [e for e in events if start <= e.get("qpc", -1) <= end]
        contacts = [e for e in in_win if e.get("event") == "native_combat_contact"]
        diags = [e for e in in_win if e.get("event") == "native_combat_diagnostics"]
        ident = [e for e in in_win if e.get("event") == "native_combat_identity"]
        inter = [e for e in in_win if e.get("event") == "native_interaction_activation"]
        hp, stam_only, kinds = [], [], {}
        for c in contacts:
            v = c["values"]
            for side in (0, 1):
                hb, ha, sb, sa = v[8 + side * 4], v[9 + side * 4], v[10 + side * 4], v[11 + side * 4]
                if hb != ha:
                    hp.append((t(c["qpc"]), side, hb, ha))
                elif sb != sa:
                    stam_only.append((t(c["qpc"]), side, sb, sa))
        for d in diags:
            v = d["values"]
            kinds[(v[1], v[2])] = kinds.get((v[1], v[2]), 0) + 1
        pairs = []
        prev = None
        for d in ident:
            v = d["values"]
            key = (v[8], v[9], v[10], v[11])
            if key != prev:
                pairs.append((t(d["qpc"]), key))
                prev = key
        print("=== %s [%s] T+%.0f..%.0fs (%d contacts, %d diags, %d interact) ==="
              % (label, state, t(start), t(end), len(contacts), len(diags), len(inter)))
        for ts, side, a, b in hp:
            print("  HIT  T+%7.1fs side%d HP %g->%g" % (ts, side, a, b))
        for ts, side, a, b in stam_only:
            print("  BLOCK T+%7.1fs side%d STAM %g->%g (hp static)" % (ts, side, a, b))
        if kinds:
            print("  KINDS:", dict(kinds))
        if len(pairs) > 1:
            print("  IDENT transitions:", len(pairs))
        if inter:
            for e in inter:
                print("  INTERACT T+%7.1fs obj %s values %s" % (t(e["qpc"]), e["object"], e["values"][:8]))
        if not hp and not stam_only and not inter:
            print("  (no HP/stamina/interaction events in window)")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
