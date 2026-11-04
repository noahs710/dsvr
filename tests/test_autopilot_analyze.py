"""Offline verdicts for tools/hmd/autopilot.py analyze.

Synthetic compact-JSON traces only; the live proof bar stays with real
captures. Layout mirrors src/core/vr_trace.cpp ContactDetour and the
decoder in tools/vr_trace.py.
"""
import json
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "hmd"))
from autopilot import verdict_for

BASE = 0x140000000
PLAYER = 0x1000000
ENEMY = 0x2000000
PLAYER_VTABLE = BASE + 0x13251F0
ENEMY_VTABLE = BASE + 0x1322E68


def check(condition, message):
    if not condition:
        raise RuntimeError(message)


def limbs(value):
    return [(value >> (16 * i)) & 0xFFFF for i in range(4)]


def header():
    return {"event": "header", "qpc": 1000, "schema": 2,
            "combat_module_base": "0000000140000000",
            "combat_health_schema": 1, "combat_health_hook": 1}


def footer(**over):
    row = {"event": "footer", "qpc": 999999, "combat_total": 1,
           "combat_self": 0, "combat_non_player": 0, "combat_detail": 1,
           "combat_detail_limit": 256}
    row.update(over)
    return row


def self_excluded(player=PLAYER):
    return {"event": "native_combat_self_excluded", "qpc": 1500,
            "values": limbs(player) + [0] * 12}


def batch(seq, qpc, flags, src_hp, tgt_hp, diag_flags=0, writes=()):
    """One contact/identity/diagnostics batch; hp pairs are (before, after)."""
    key = {"qpc": qpc, "thread": 7, "seq": seq, "object": "15f0a0"}
    values = (limbs(PLAYER) + limbs(ENEMY) +
              [src_hp[0], src_hp[1], 80, 80, tgt_hp[0], tgt_hp[1], 60, 60])
    rows = [
        {"event": "native_combat_contact", "flags": flags, "native_rate": 1,
         "values": values, **key},
        {"event": "native_combat_identity", "flags": flags, "native_rate": 1,
         "values": (limbs(PLAYER_VTABLE) + limbs(ENEMY_VTABLE) +
                    [0, 16, 8, 16, 1, 0, 0, 0]), **key},
        {"event": "native_combat_diagnostics", "flags": diag_flags,
         "native_rate": 1, "values": [0] * 15 + [2], **key},
    ]
    for actor, requested in writes:
        rows.append({"event": "native_combat_health_write", "flags": 0,
                     "native_rate": 1,
                     "values": (limbs(actor) + limbs(BASE + 0x322A9D) +
                                [requested & 0xFFFF, (requested >> 16) & 0xFFFF,
                                 0, 1, 0, 0, 1]), **key})
    return rows


def analyze(rows):
    with tempfile.TemporaryDirectory() as temporary:
        path = Path(temporary) / "trace.jsonl"
        # The native writer emits compact JSON; the scanner prefilters on it.
        path.write_text("".join(json.dumps(r, separators=(",", ":")) + "\n"
                                for r in rows))
        return verdict_for(path)


def check_contact_drop_passes():
    v = analyze([header(), self_excluded(),
                 *batch(1, 2000, 15, (659, 659), (90, 70)), footer()])
    check(v["verdict"] == "PASS", f"expected PASS, got {v}")
    check(v["hp_drops"][0]["via"] == "contact", f"drop via contact: {v}")
    check(v["complete"] is True, f"footer present: {v}")


def check_sequence_drop_passes():
    v = analyze([header(), self_excluded(),
                 *batch(1, 2000, 15, (659, 659), (90, 90)),
                 *batch(2, 3000, 15, (659, 659), (70, 70)), footer()])
    check(v["verdict"] == "PASS", f"expected PASS, got {v}")
    check(v["hp_drops"][0]["via"] == "sequence", f"drop via sequence: {v}")


def check_health_write_drop_passes():
    v = analyze([header(), self_excluded(),
                 *batch(1, 2000, 3, (659, 659), (90, 90),
                        writes=[(ENEMY, 50)]), footer()])
    check(v["verdict"] == "PASS", f"expected PASS, got {v}")
    check(v["hp_drops"][0]["via"] == "health_write", f"drop via write: {v}")


def check_footerless_still_passes_but_incomplete():
    v = analyze([header(), self_excluded(),
                 *batch(1, 2000, 15, (659, 659), (44, 31))])
    check(v["verdict"] == "PASS", f"expected PASS, got {v}")
    check(v["complete"] is False, f"no footer: {v}")
    check(v["combat_total"] is None, f"counters unknown: {v}")


def check_unreadable_enemy_is_unverified_with_reasons():
    diag = 3 << 4 | 9 << 12  # target before=Type, after=NotAdmitted
    v = analyze([header(), self_excluded(),
                 *batch(1, 2000, 5, (659, 659), (0, 0), diag_flags=diag),
                 footer(combat_total=42, combat_self=40, combat_detail=2)])
    check(v["verdict"] == "UNVERIFIED", f"expected UNVERIFIED, got {v}")
    check(v["read_reasons"] == {"unqualified_actor_type": 1,
                                "not_admitted_before_call": 1},
          f"reasons: {v}")
    check(v["player_contacts"] == 1 and v["distinct_targets"] == 1, f"{v}")


def check_player_damage_is_not_a_pass():
    # Enemy as source: swap sides by pointing self_excluded at the target slot.
    rows = [header(), self_excluded(player=ENEMY)]
    for row in batch(1, 2000, 15, (90, 90), (659, 600)):
        rows.append(row)
    rows.append(footer())
    v = analyze(rows)
    check(v["verdict"] == "UNVERIFIED", f"player hurt must not PASS: {v}")
    check(len(v["player_damage_taken"]) == 1, f"damage recorded: {v}")


def check_unpaired_drop_does_not_pass():
    rows = [header(), self_excluded(),
            *batch(1, 2000, 15, (659, 659), (90, 70)), footer()]
    rows = [r for r in rows if r["event"] != "native_combat_identity"]
    v = analyze(rows)
    check(v["verdict"] == "UNVERIFIED", f"unpaired drop must not PASS: {v}")
    check(v["pairing_errors"] >= 1, f"pairing error counted: {v}")


def check_no_contacts():
    v = analyze([header(), footer(combat_total=0, combat_detail=0)])
    check(v["verdict"] == "NO_CONTACTS", f"expected NO_CONTACTS, got {v}")


def check_empty_is_malformed():
    v = analyze([])
    check(v["verdict"] == "MALFORMED", f"expected MALFORMED, got {v}")


def main():
    checks = [value for name, value in sorted(globals().items())
              if name.startswith("check_") and callable(value)]
    for fn in checks:
        fn()
    print("test_autopilot_analyze: checks passed")


if __name__ == "__main__":
    main()
