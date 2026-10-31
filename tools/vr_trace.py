"""Validate explicit view/image provenance; never infer it from Present FPS.

The current native probes emit camera_command/render_enter observations, not
view_consumed. Such captures intentionally report incomplete acceptance.
"""
import argparse
import json
from collections import Counter
import math
import statistics
from pathlib import Path


def _integer(value, minimum=0):
    return type(value) is int and minimum <= value <= (1 << 64) - 1


def _finite_vector(value, size):
    if not isinstance(value, list) or len(value) != size:
        return False
    try:
        return all(type(v) in (int, float) and math.isfinite(v) for v in value)
    except OverflowError:
        return False


def _signed_state(limbs):
    value = int(limbs[0]) | (int(limbs[1]) << 16)
    return value if value < (1 << 31) else value - (1 << 32)


def _record_ok(event):
    if not isinstance(event, dict) or not isinstance(event.get("event"), str) or \
            not event["event"] or not _integer(event.get("qpc")):
        return False
    for field in ("duration_qpc", "period_ns"):
        if field in event and not _integer(event[field]):
            return False
    if "values" in event and not _finite_vector(event["values"], 16):
        return False
    kind = event["event"]
    if kind == "header":
        if "capture_domain" in event and event["capture_domain"] not in ("full", "combat", "session"):
            return False
        if any(field in event and (type(event[field]) is not int or event[field] not in (0, 1))
                for field in ("combat_detail_schema", "combat_context_schema", "combat_hook",
                              "combat_health_schema", "combat_health_hook")):
            return False
        if "combat_module_base" in event and (not isinstance(event["combat_module_base"], str) or
                len(event["combat_module_base"]) != 16 or
                any(c not in "0123456789abcdefABCDEF" for c in event["combat_module_base"])):
            return False
    if kind in ("tracking", "view_consumed", "submit"):
        if not all(_integer(event.get(field), 1) for field in
                   ("seq", "epoch", "predicted_ns")):
            return False
    if kind == "view_consumed":
        if not _integer(event.get("frame"), 1) or \
                type(event.get("eye")) is not int or event["eye"] not in (0, 1):
            return False
    if kind == "submit" and "frame" in event and not _integer(event["frame"]):
        return False
    if kind == "footer" and not _integer(event.get("dropped")):
        return False
    if kind == "footer":
        if "combat_self" in event:
            fields = ("combat_total", "combat_non_player", "combat_self",
                      "combat_refused", "combat_detail", "combat_detail_limit")
            if any(not _integer(event.get(k)) for k in fields) or \
                    event["combat_total"] != sum(event[k] for k in fields[1:5]) or \
                    event["combat_detail"] > event["combat_detail_limit"]:
                return False
        if "queue_peak" in event and not _integer(event["queue_peak"]):
            return False
        if event.get("queue_peak", 0) > 4096:
            return False
        if "contended" in event and (not _integer(event["contended"]) or
                                      event["contended"] > event["dropped"]):
            return False
    if kind == "native_interaction_activation":
        flags = event.get("flags")
        values = event.get("values")
        if not _integer(event.get("seq"), 1) or not _integer(event.get("thread"), 1) or \
                type(flags) is not int or not 0 <= flags <= 15 or \
                not _finite_vector(values, 16) or \
                any(type(v) not in (int, float) or int(v) != v or not 0 <= v <= 65535
                    for v in values) or \
                (flags & 4 and flags & 3 != 3) or (flags & 8 and not flags & 1):
            return False
    if kind == "combat_bench_provisional":
        if not _integer(event.get("seq"), 1) or not _integer(event.get("epoch"), 1) or \
                type(event.get("flags")) is not int or not 0 <= event["flags"] <= 15 or \
                not _finite_vector(event.get("values"), 16):
            return False
    if kind in ("host_source", "host_swapchain", "host_device"):
        if not _integer(event.get("frame"), 1) or \
                type(event.get("flags")) is not int or event["flags"] not in (1, 2) or \
                not _finite_vector(event.get("values"), 16):
            return False
        limbs = event["values"][:4]
        if any(not 0 <= v <= 65535 or int(v) != v for v in limbs) or not any(limbs):
            return False
        owner = event.get("object")
        if not isinstance(owner, str) or not owner or len(owner) > 16 or \
                any(c not in "0123456789abcdefABCDEF" for c in owner) or \
                int(owner, 16) == 0:
            return False
        # These tags know resource lifetime, not native scene or XR prediction.
        if any(event.get(k, 0) != 0 for k in ("epoch", "seq", "predicted_ns")):
            return False
    if kind == "native_input_blockers":
        if not _integer(event.get("seq"), 1) or \
                type(event.get("flags")) is not int or event["flags"] not in (0, 1) or \
                not _finite_vector(event.get("values"), 16):
            return False
        if any(type(v) not in (int, float) or not 0 <= v <= 65535 or int(v) != v
               for v in event["values"][:4]):
            return False
        owner = event.get("object")
        if not isinstance(owner, str) or not owner or len(owner) > 16 or \
                any(c not in "0123456789abcdefABCDEF" for c in owner):
            return False
        if event["flags"] != int(int(owner, 16) != 0):
            return False
    if kind == "native_step_dispatch":
        if not _integer(event.get("seq"), 1) or \
                type(event.get("flags")) is not int or event["flags"] not in (1, 2, 3) or \
                not _integer(event.get("duration_qpc")) or \
                not _finite_vector(event.get("values"), 16):
            return False
        if any(not 0 <= v <= 65535 or int(v) != v for v in event["values"][:4]) or \
                event["values"][5] not in (0, 1, 2, 3):
            return False
        owner = event.get("object")
        if not isinstance(owner, str) or not owner or len(owner) > 16 or \
                any(c not in "0123456789abcdefABCDEF" for c in owner) or \
                int(owner, 16) == 0:
            return False
    if kind == "native_combat_context":
        flags, owner, values = event.get("flags"), event.get("object"), event.get("values")
        if not _integer(event.get("seq"), 1) or not _integer(event.get("thread"), 1) or \
                type(flags) is not int or not 0 <= flags <= 7 or \
                not _integer(event.get("native_rate")) or event["native_rate"] > 0xffffffff or \
                not isinstance(owner, str) or not owner or len(owner) > 16 or \
                any(c not in "0123456789abcdefABCDEF" for c in owner) or \
                int(owner, 16) == 0 or not _finite_vector(values, 16):
            return False
        if any(type(v) not in (int, float) or not 0 <= v <= 65535 or int(v) != v
               for v in values[:12]) or \
                any(type(v) not in (int, float) or not 0 <= v <= 255 or int(v) != v
                    for v in values[12:14]) or \
                any(type(v) not in (int, float) or not 0 <= v <= 65535 or int(v) != v
                    for v in values[14:]):
            return False
        if (not flags & 1 and any(values[:12])) or (not flags & 2 and any(values[12:14])) or \
                (not flags & 4 and any(values[14:])):
            return False
        return True
    if kind == "native_combat_motion":
        flags = event.get("flags")
        owner = event.get("object")
        if not _integer(event.get("seq"), 1) or not _integer(event.get("thread"), 1) or \
                type(flags) is not int or not 0 <= flags <= 31 or \
                not _integer(event.get("native_rate")) or event["native_rate"] > 0xffffffff or \
                not isinstance(owner, str) or not owner or len(owner) > 16 or \
                any(c not in "0123456789abcdefABCDEF" for c in owner) or \
                int(owner, 16) == 0 or \
                not _finite_vector(event.get("values"), 16):
            return False
        values = event["values"]
        # A read that failed leaves its fields zeroed, so a number without the
        # matching read bit is a producer bug, not an unmeasured quantity.
        if any(v != 0 for v in values[10:]) or values[8] < 0 or values[9] < 0:
            return False
        if not flags & 1 and any(v != 0 for v in values[:4]):
            return False
        if not flags & 2 and any(v != 0 for v in values[4:8]):
            return False
        if not flags & 4 and values[8] != 0:
            return False
        if not flags & 16 and values[9] != 0:
            return False
        if flags & 8 and not 0.9 <= values[8] <= 1.1:
            return False
        return True

    if kind in ("native_combat_contact", "native_combat_identity",
                "native_combat_diagnostics", "native_combat_health_write"):
        flags = event.get("flags")
        owner = event.get("object")
        if not _integer(event.get("seq"), 1) or not _integer(event.get("thread"), 1) or \
                type(flags) is not int or \
                not isinstance(owner, str) or not owner or len(owner) > 16 or \
                any(c not in "0123456789abcdefABCDEF" for c in owner) or \
                not _finite_vector(event.get("values"), 16) or \
                not _integer(event.get("native_rate")) or event["native_rate"] > 0xffffffff:
            return False
        values = event["values"]
        if kind == "native_combat_health_write":
            return (flags == 0 and
                    all(type(v) in (int, float) and 0 <= v <= 65535 and int(v) == v
                        for v in values[:10]) and
                    type(values[10]) in (int, float) and 0 <= values[10] <= 3 and
                    int(values[10]) == values[10] and
                    all(type(v) in (int, float) and 0 <= v <= 65535 and int(v) == v
                        for v in values[11:13]) and
                    type(values[13]) in (int, float) and values[13] in (0, 1) and
                    values[14] == 0 and values[15] == 1 and
                    _limbs(values[11:13]) > values[10])
        if kind == "native_combat_diagnostics":
            if not (0 <= flags <= 0xffff and
                    all(((flags >> shift) & 15) <= 9 for shift in (0, 4, 8, 12)) and
                    _integer(event.get("duration_qpc"))):
                return False
            if all(v == 0 for v in values):  # legacy: response fields unavailable
                return True
            if values[15] not in (1, 2):
                return False
            if values[15] == 1 and any(v != 0 for v in values[9:15]):
                return False
            limits = [31] + [65535]*4 + [255]*4
            if any(not 0 <= value <= limit or int(value) != value
                   for value, limit in zip(values[:9], limits)):
                return False
            if values[15] == 2:
                # v2 store group: raw float weight + int limbs + tag byte.
                if not (isinstance(values[9], (int, float)) and
                        all(type(v) in (int, float) and 0 <= v <= 65535 and int(v) == v
                            for v in values[10:14]) and
                        type(values[14]) in (int, float) and 0 <= values[14] <= 255 and
                        int(values[14]) == values[14]):
                    return False
            mask = int(values[0])
            groups = [(1,1,3), (2,3,5), (4,5,7), (8,7,9)]
            if values[15] == 2:
                groups.append((16,9,15))
            return all(mask & bit or not any(values[start:end])
                       for bit, start, end in groups)
        if not 0 <= flags <= 15 or (flags >> 2) & ~flags:
            return False
        limb_count = 16 if kind == "native_combat_identity" else 8
        if any(type(v) not in (int, float) or not 0 <= v <= 65535 or int(v) != v
               for v in values[:limb_count]):
            return False
        if kind == "native_combat_contact" and any(
                type(v) not in (int, float) or not 0 <= v <= 10000000 or int(v) != v
                for v in values[8:]):
            return False
    return True


def _limbs(values):
    return sum(int(value) << (16 * index) for index, value in enumerate(values))


def _case_plan(path, header):
    """Load operator-declared case windows on the capture's own QPC clock.

    A label is an operator declaration, never an observation: binding is only a
    time window. The clock must be the same one the capture header reports, or
    the windows would silently select the wrong samples. The declared duration
    and completion state are carried too: a window cut short by the transaction
    deadline or a stop request is a different kind of evidence from one the
    operator actually had the full time for, and the file alone cannot tell them
    apart without the plan's own `seconds`.
    """
    try:
        document = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, ValueError):
        return (), (f"case plan unreadable: {path}",), {}
    errors = []
    if not isinstance(document, dict) or document.get("schema") != 1:
        return (), ("case plan has no supported schema",), {}
    hz = document.get("qpc_hz")
    if not _integer(hz, 1):
        errors.append("case plan clock is invalid")
    elif type(header.get("qpc_hz")) is int and hz != header["qpc_hz"]:
        errors.append("case plan clock differs from capture clock")
    rows = document.get("cases")
    if not isinstance(rows, list) or not rows:
        errors.append("case plan declares no cases")
        rows = []
    windows, labels, declared = [], [], {}
    for row in rows:
        if not isinstance(row, dict) or not isinstance(row.get("label"), str) or \
                not row["label"] or not _integer(row.get("start_qpc"), 1) or \
                not _integer(row.get("end_qpc"), 1) or row["end_qpc"] < row["start_qpc"]:
            errors.append("case plan declares a malformed window")
            continue
        if row["label"] in labels:
            errors.append(f"case plan repeats label {row['label']}")
        labels.append(row["label"])
        seconds = row.get("seconds")
        state = row.get("state")
        declared[row["label"]] = {
            "seconds": seconds if _integer(seconds, 1) else None,
            "state": state if isinstance(state, str) and state else None}
        windows.append((row["label"], row["start_qpc"], row["end_qpc"]))
    windows.sort(key=lambda window: window[1])
    for previous, current in zip(windows, windows[1:]):
        if current[1] < previous[2]:
            errors.append("case plan windows overlap")
            break
    return (() if errors else windows), errors, declared


def _timing_summary(ticks, frequency):
    if not ticks or frequency <= 0:
        return None
    values = sorted(value * 1000 / frequency for value in ticks)
    return dict(count=len(values), median_ms=statistics.median(values),
                p95_ms=values[math.ceil(len(values) * .95) - 1],
                p99_ms=values[math.ceil(len(values) * .99) - 1], max_ms=values[-1])


# How far a stamped window boundary may drift from the duration the plan asked
# for before the window counts as cut short. The runner stamps both boundaries
# from the same QPC clock, so the only real slack is the 250 ms poll that ends
# the wait; anything larger means the deadline or a stop request ended the case.
_TRUNCATION_SLACK_SECONDS = 1.0


def _case_coverage(cases, declared, span, frequency):
    """Per declared window: how much of it this capture actually recorded.

    A capture is opened and closed by the operator (F9), so it can miss a window
    entirely, cover part of it, or cover all of it. Without this, a window with
    zero samples reads as "the declared action produced no native contact", when
    the honest reading is "nothing was recorded while it happened". The capture
    span is header..footer on the capture's own clock; an unclosed capture has no
    span, so coverage stays unknown rather than guessed.
    """
    coverage = {}
    for label, start, end in cases:
        plan = declared.get(label) or {}
        seconds = plan.get("seconds")
        measured = (end - start) / frequency if frequency else None
        row = {"window_qpc": [start, end], "plan_state": plan.get("state"),
               "declared_seconds": seconds, "measured_seconds": measured,
               "truncated": bool(seconds is not None and measured is not None and
                                 measured + _TRUNCATION_SLACK_SECONDS < seconds)}
        if span is None or not frequency:
            row["capture_overlap_ms"] = None
            row["capture_covers_window"] = None
        else:
            row["capture_overlap_ms"] = max(
                0, min(end, span[1]) - max(start, span[0])) * 1000 / frequency
            row["capture_covers_window"] = span[0] <= start and span[1] >= end
        coverage[label] = row
    return coverage


# A native contact callback can lag the pose commit that produced the tracked
# swing, so proximity is measured in wall time, not frame counts. This window is
# a correlation radius, never a causal claim.
_ATTRIBUTION_WINDOW_MS = 150


def _combat_observations(records, require_diagnostics=False, frequency=0, cases=(),
                         require_context=False, health_schema=0, health_hook=0):
    contacts, identities, diagnostics, motion, contexts, health = {}, {}, {}, {}, {}, {}
    groups = {"native_combat_contact": contacts, "native_combat_identity": identities,
              "native_combat_diagnostics": diagnostics, "native_combat_motion": motion,
              "native_combat_context": contexts, "native_combat_health_write": health}
    for record in records:
        kind = record["event"]
        if kind not in groups:
            continue
        key = (record["thread"], record["seq"], int(record["object"], 16))
        groups[kind].setdefault(key, []).append(record)
    samples, errors, reasons = [], [], Counter()
    native_ticks, observer_ticks = [], []
    diagnostic_samples = motion_samples = context_samples = 0
    reason_names = ("valid", "invalid_address", "vtable_unreadable_or_outside_image",
                    "unqualified_actor_type", "handle_unreadable", "stats_unreadable",
                    "stats_out_of_range", "record_unreadable", "actor_changed",
                    "not_admitted_before_call")
    for key in sorted(contacts.keys() | identities.keys() | diagnostics.keys() |
            motion.keys() | contexts.keys() | health.keys()):
        outcome, identity = contacts.get(key, []), identities.get(key, [])
        if len(outcome) != 1 or len(identity) != 1:
            errors.append(f"contact identity pairing {key}: {len(outcome)} contacts, "
                          f"{len(identity)} identities")
            continue
        outcome, identity = outcome[0], identity[0]
        native_return = _limbs(identity["values"][12:16])
        if outcome["flags"] != identity["flags"] or \
                native_return & 0xffffffff != outcome["native_rate"] or \
                identity["native_rate"] != outcome["native_rate"]:
            errors.append(f"contact identity payload disagrees {key}")
            continue
        diagnostic = diagnostics.get(key, [])
        codes = None
        if len(diagnostic) > 1 or (require_diagnostics and len(diagnostic) != 1):
            errors.append(f"contact diagnostics pairing {key}: {len(diagnostic)} records")
        elif diagnostic:
            diagnostic = diagnostic[0]
            codes = [(diagnostic["flags"] >> shift) & 15 for shift in (0, 4, 8, 12)]
            expected = sum((code == 0) << bit for bit, code in enumerate(codes))
            if expected != outcome["flags"] or diagnostic["native_rate"] != outcome["native_rate"]:
                errors.append(f"contact diagnostics validity disagrees {key}")
            else:
                diagnostic_samples += 1
                reasons.update(reason_names[code] for code in codes)
                observer_ticks.append(diagnostic["duration_qpc"])
        sample = {"thread": key[0], "seq": key[1], "record": outcome["object"],
                  "qpc": outcome["qpc"],
                  "native_return": f"{native_return:016x}",
                  "read_reasons": [reason_names[code] for code in codes] if codes is not None else None,
                  "case": next((label for label, start, end in cases
                                if start <= outcome["qpc"] <= end), None)}
        sample["native_response"] = None
        if isinstance(diagnostic, dict) and diagnostic["values"][15] in (1, 2):
            values = diagnostic["values"]
            mask = int(values[0])
            sample["native_response"] = {
                "schema": int(values[15]),
                "kind_before": _limbs(values[1:3]) if mask & 1 else None,
                "kind_after": _limbs(values[3:5]) if mask & 2 else None,
                "status_before": [int(v) for v in values[5:7]] if mask & 4 else None,
                "status_after": [int(v) for v in values[7:9]] if mask & 8 else None,
            }
            if int(values[15]) == 2:
                sample["native_response"].update({
                    "store_weight_after": float(values[9]) if mask & 16 else None,
                    "store_paired_ac": _limbs(values[10:12]) if mask & 16 else None,
                    "store_paired_b0": _limbs(values[12:14]) if mask & 16 else None,
                    "store_tag_b4": int(values[14]) if mask & 16 else None,
                })
        context_rows = contexts.get(key, [])
        sample["native_context"] = None
        if len(context_rows) > 1 or (require_context and len(context_rows) != 1):
            errors.append(f"contact context pairing {key}: {len(context_rows)} records")
        elif context_rows:
            context = context_rows[0]
            if context["native_rate"] != outcome["native_rate"]:
                errors.append(f"contact context payload disagrees {key}")
            else:
                flags, payload = context["flags"], context["values"]
                context_samples += 1
                sample["native_context"] = {
                    "tuple_read": bool(flags & 1), "flags_read": bool(flags & 2),
                    **{name: _signed_state(payload[i*2:i*2+2]) if flags & 1 else None
                       for i, name in enumerate(("attack_type", "behavior_id", "attack_param_id",
                                                 "table_selector", "geometry_selector", "weapon_id"))},
                    "payload_flags_c8": int(payload[12]) if flags & 2 else None,
                    "payload_flags_c9": int(payload[13]) if flags & 2 else None,
                    "event_handle": _limbs(payload[14:16]) if flags & 4 else None}
        for side, name in enumerate(("source", "target")):
            before = bool(outcome["flags"] & (1 << side))
            stable = bool(outcome["flags"] & (1 << (side + 2)))
            values = outcome["values"][8 + side * 4:12 + side * 4]
            sample[name] = {
                "address": f'{_limbs(outcome["values"][side * 4:side * 4 + 4]):016x}',
                "vtable": f'{_limbs(identity["values"][side * 4:side * 4 + 4]):016x}' if before else None,
                "handle": _limbs(identity["values"][8 + side * 2:10 + side * 2]) if before else None,
                "stable_through_call": stable,
                "hp_before": int(values[0]) if before else None,
                "hp_after": int(values[1]) if stable else None,
                "hp_delta": int(values[1] - values[0]) if stable else None,
                "stamina_before": int(values[2]) if before else None,
                "stamina_after": int(values[3]) if stable else None,
                "stamina_delta": int(values[3] - values[2]) if stable else None}
        health_rows = health.get(key, [])
        parsed_writes = []
        if len(health_rows) > 4:
            errors.append(f"contact health-write bound exceeded {key}: {len(health_rows)} records")
        if health_rows and (health_schema != 1 or health_hook != 1):
            errors.append(f"health writes present without enabled observer schema {key}")
        total_calls = None
        indices = set()
        for row in health_rows:
            payload = row["values"]
            count = _limbs(payload[11:13])
            index = int(payload[10])
            if row["native_rate"] != outcome["native_rate"]:
                errors.append(f"contact health-write return disagrees {key}")
            if total_calls is not None and total_calls != count:
                errors.append(f"contact health-write call counts disagree {key}")
            total_calls = count
            if index in indices:
                errors.append(f"duplicate contact health-write index {key}: {index}")
            indices.add(index)
            actor = _limbs(payload[0:4])
            caller = _limbs(payload[4:8])
            side = ("source" if actor == int(sample["source"]["address"], 16) else
                    "target" if actor == int(sample["target"]["address"], 16) else None)
            parsed_writes.append({
                "actor": f"{actor:016x}", "actor_side": side,
                "caller": f"{caller:016x}",
                "requested_hp": _signed_state(payload[8:10]),
                "index": index, "total_scoped_calls": count,
                "truncated": bool(payload[13])})
        if total_calls is not None:
            expected = min(total_calls, 4)
            if len(health_rows) != expected or indices != set(range(expected)):
                errors.append(f"contact health-write sequence incomplete {key}")
            if any(bool(row["values"][13]) != (total_calls > 4) for row in health_rows):
                errors.append(f"contact health-write truncation marker disagrees {key}")
        sample["health_writer_observation"] = {
            "state": ("writes_observed" if health_rows else
                      "no_setter_observed_in_scoped_callback" if health_schema == 1 and health_hook == 1
                      else "unknown_or_observer_unavailable"),
            "recorded": parsed_writes, "scoped_call_count": total_calls,
            "truncated": bool(total_calls and total_calls > 4)}
        # Motion is optional extra evidence: a capture from an older build carries
        # none and its absence must not invalidate a contact pairing. More than one
        # motion record for one callback, or a motion record bound to a different
        # native return, is a real disagreement and is reported.
        motion_rows = motion.get(key, [])
        if len(motion_rows) > 1:
            errors.append(f"contact motion pairing {key}: {len(motion_rows)} records")
            motion_rows = []
        motion_record = motion_rows[0] if motion_rows else None
        if motion_record is not None and \
                motion_record["native_rate"] != outcome["native_rate"]:
            errors.append(f"contact motion payload disagrees {key}")
            motion_record = None
        sample["native_motion"] = None
        if motion_record is not None:
            flags, payload = motion_record["flags"], motion_record["values"]
            sample["native_motion"] = {
                "flags": flags,
                "direction_read": bool(flags & 1), "velocity_read": bool(flags & 2),
                "direction_unit": bool(flags & 8),
                "direction": list(payload[0:3]) if flags & 4 else None,
                "direction_length": payload[8] if flags & 4 else None,
                "velocity": list(payload[4:7]) if flags & 16 else None,
                # Native units per second, not metres: the native field is
                # (current - previous) * 60.0 in the game's own world units.
                "speed_units_per_s": payload[9] if flags & 16 else None}
            motion_samples += 1
        samples.append(sample)
        if "duration_qpc" in outcome:
            native_ticks.append(outcome["duration_qpc"])
    case_observations = {}
    for label, start, end in cases:
        rows = [sample for sample in samples if sample["case"] == label]
        case_observations[label] = {
            "window_qpc": [start, end], "samples": len(rows),
            "context_samples": sum(sample["native_context"] is not None for sample in rows),
            "event_handles": sorted({sample["native_context"]["event_handle"] for sample in rows
                                     if sample["native_context"] and
                                     sample["native_context"]["event_handle"] is not None}),
            "attack_selections": [dict(zip(("attack_type", "behavior_id", "attack_param_id",
                                             "table_selector", "weapon_id"), selection))
                                  for selection in sorted({tuple(sample["native_context"][key]
                                      for key in ("attack_type", "behavior_id", "attack_param_id",
                                                  "table_selector", "weapon_id"))
                                      for sample in rows if sample["native_context"] and
                                      sample["native_context"]["tuple_read"]})],
            "diagnostic_samples": sum(row["read_reasons"] is not None for row in rows),
            "read_reason_counts": dict(Counter(
                reason for row in rows for reason in (row["read_reasons"] or ()))),
            "samples_with_hp_change": sum(bool(sample["target"]["hp_delta"]) for sample in rows),
            "samples_with_hp_loss": sum((sample["target"]["hp_delta"] or 0) < 0 for sample in rows),
            "stable_samples": sum(sample["target"]["stable_through_call"] for sample in rows),
            "target_handles": sorted({sample["target"]["handle"] for sample in rows
                                      if sample["target"]["handle"]}),
            "hp_delta_values": sorted({sample["target"]["hp_delta"] for sample in rows
                                       if sample["target"]["hp_delta"] is not None}),
            "stamina_delta_values": sorted({sample["target"]["stamina_delta"] for sample in rows
                                            if sample["target"]["stamina_delta"] is not None}),
              "native_motion_samples": sum(1 for sample in rows
                                          if sample["native_motion"]),
              "native_unit_direction_samples": sum(1 for sample in rows
                                                   if sample["native_motion"] and
                                                   sample["native_motion"]["direction_unit"]),
              "native_speed_units_per_s_values": sorted(
                  {sample["native_motion"]["speed_units_per_s"]
                   for sample in rows
                   if sample["native_motion"] and
                   sample["native_motion"]["speed_units_per_s"] is not None})}
    # Join the tracked-blade stream with the native callbacks per declared case.
    # The two streams share one QPC clock, so a swing we measured and a native
    # contact can be placed next to each other; that is the only thing this
    # establishes. The bench itself still runs on assumed .9 m blade geometry
    # and synthetic targets, so a nearby classification is not a native cause.
    bench = [e for e in records if e["event"] == "combat_bench_provisional"]
    span = int(round(frequency * _ATTRIBUTION_WINDOW_MS / 1000.0)) if frequency else 0
    attribution = {}
    for label, start, end in cases:
        rows = [sample for sample in samples if sample["case"] == label]
        window = [e for e in bench if start <= e["qpc"] <= end]
        proximity = []
        for sample in rows:
            pool = [e for e in bench if span and abs(e["qpc"] - sample["qpc"]) <= span]
            near = min(pool, key=lambda e: abs(e["qpc"] - sample["qpc"]), default=None)
            # The native velocity is (current - previous) * 60.0 in native world
            # units: a fixed 60 Hz assumption inside the native function, not a
            # measured rate. It is only comparable with our metres-per-second tip
            # speed after dividing by the world scale that was actually applied.
            scale = near["values"][13] if near else None
            units_per_s = (sample["native_motion"]["speed_units_per_s"]
                           if sample["native_motion"] else None)
            native_mps = (units_per_s / scale if units_per_s is not None
                          and scale is not None and scale > 0 else None)
            proximity.append({
                "contact_qpc": sample["qpc"],
                "hp_delta": sample["target"]["hp_delta"],
                "bench_qpc": near["qpc"] if near else None,
                "lag_ms": (round((sample["qpc"] - near["qpc"]) / frequency * 1000.0, 3)
                           if near and frequency else None),
                "bench_tip_speed_mps": (max(near["values"][0], near["values"][1])
                                        if near else None),
                "bench_geometry_hit": bool(near["flags"] & 1) if near else None,
                "world_scale": scale,
                "native_speed_units_per_s": units_per_s,
                "native_speed_mps": native_mps,
                "native_direction_unit": (sample["native_motion"]["direction_unit"]
                                          if sample["native_motion"] else None)})
        attribution[label] = {
            "window_qpc": [start, end],
            "near_window_ms": _ATTRIBUTION_WINDOW_MS,
            "bench_samples": len(window),
            "bench_classification_frames": {
                name: sum(bool(e["flags"] & bit) for e in window)
                for name, bit in (("geometry_hit", 1), ("block", 2), ("parry", 4))},
            "bench_max_tip_speed_mps": max(
                (max(e["values"][0], e["values"][1]) for e in window), default=None),
            "contacts": len(rows),
            "native_motion_samples": sum(1 for row in rows if row["native_motion"]),
            "native_speed_units_per_s_values": sorted(
                {row["native_motion"]["speed_units_per_s"]
                 for row in rows
                 if row["native_motion"] and
                 row["native_motion"]["speed_units_per_s"] is not None}),
            "native_speed_mps_values": sorted(
                {near["native_speed_mps"] for near in proximity
                 if near["native_speed_mps"] is not None}),
            # Our measured tip speed for the same swing against the native record's
            # own speed converted to metres per second. Agreement corroborates the
            # measurement and the applied world scale; it is still not proof that the
            # native value is what produced the damage. A missing world scale leaves
            # the native number in raw units and this comparison empty.
            "bench_minus_native_speed_mps": sorted(
                {round(near["bench_tip_speed_mps"] - near["native_speed_mps"], 6)
                 for near in proximity
                 if near["bench_tip_speed_mps"] is not None and
                 near["native_speed_mps"] is not None}),
            "native_speed_without_world_scale": sum(
                1 for near in proximity
                if near["native_speed_units_per_s"] is not None and
                not near["world_scale"]),
            "contacts_with_hp_loss": sum((s["target"]["hp_delta"] or 0) < 0 for s in rows),
            "contact_proximity": proximity,
            # An HP loss with no tracked classification nearby came from something
            # we did not measure (native NPC traffic, a spell, a fall). Counting it
            # separately is what stops a capture full of other actors' damage from
            # reading as our sword working.
            "hp_loss_without_near_tracked_hit": sum(
                1 for row, near in zip(rows, proximity)
                if (row["target"]["hp_delta"] or 0) < 0 and not near["bench_geometry_hit"]),
            "hp_loss_without_any_bench_sample": sum(
                1 for row in rows
                if (row["target"]["hp_delta"] or 0) < 0 and not bench)}
    return {"status": "UNVERIFIED", "paired_samples": len(samples),
            "health_writer_capability": {
                "schema": health_schema, "hook": health_hook,
                "state": ("enabled" if health_schema == 1 and health_hook == 1 else
                          "disabled_or_legacy_unknown")},
            "motion_samples": motion_samples, "context_samples": context_samples,
            "case_observations": case_observations,
            "case_attribution": attribution,
            "diagnostic_samples": diagnostic_samples, "read_reason_counts": dict(reasons),
            "timings": {name: summary for name, ticks in
                        (("native_call", native_ticks), ("observer_read", observer_ticks))
                        if (summary := _timing_summary(ticks, frequency)) is not None},
            "unpaired_contacts": sum(len(rows) for key, rows in contacts.items()
                                     if key not in identities),
            "pairing_errors": errors, "samples": samples,
            "contact_limit_reached": any(e["event"] == "native_combat_limit" for e in records),
            "detail": "Natural-call observations only. HP/stamina changes and raw return "
                      "bits do not identify a weapon hit, native defense, attack context "
                      "or a qualified physical-damage binding. Selected behavior/attack IDs, "
                      "table selectors and payload flags are caller-context observations, "
                      "not proof of guard/parry success or a callable attack ABI. "
                      "Source/target are raw "
                      "record-side labels, not verified attacker/victim roles. The motion "
                      "fields sit in the record the caller builds at RSP+0x20: the pinned "
                      "program's own decompile places a normalized direction (w forced to "
                      "zero, components zeroed or infinite when the source length was "
                      "degenerate) at +0xF0 and a (current - previous) * 60.0 velocity in "
                      "native world units at +0x150. That layout is static evidence from "
                      "the pinned program, not a runtime observation: the read and "
                      "unit-length bits report what a real capture actually showed, and "
                      "the velocity's 60 Hz assumption is native's, not measured. "
                      "Observer read cost excludes native execution, queue admission and "
                      "writer I/O."}


def validate(events):
    """Check tag correspondence, NOT the truth of the claimed GPU observations.

    valid describes these checks only. complete remains false until separate
    native/GPU/comfort acceptance is available; arbitrary JSON cannot certify it.

    A combat-domain capture suspends XR submission recording by design, so an
    empty submission stream there is expected coverage, not an invalid capture.
    """
    errors, tracking, views, submitted = Counter(), {}, {}, set()
    records = []
    if not isinstance(events, (list, tuple)):
        events = [None]
    for event in events:
        if not _record_ok(event):
            errors["malformed event"] += 1
        else:
            records.append(event)
    domain = next((event.get("capture_domain", "legacy-unspecified")
                   for event in records if event["event"] == "header"),
                  "legacy-unspecified")
    submits = repeats = 0
    epoch = sequence = 0
    mono = False
    for event in sorted(records, key=lambda e: e["qpc"]):
        kind = event["event"]
        if kind not in ("tracking", "view_consumed", "submit", "footer"):
            continue
        if kind == "footer":
            if event["dropped"]:
                errors["capture dropped events"] += 1
            if event.get("interrupted") is True:
                errors["capture interrupted"] += 1
            if event.get("io_error") is True:
                errors["capture I/O failed"] += 1
            continue
        key = (event["epoch"], event["seq"])
        if kind == "tracking":
            if key in tracking:
                errors["duplicate tracking identity"] += 1
            if event["epoch"] < epoch or event["seq"] <= sequence:
                errors["tracking generation went backwards"] += 1
            epoch = max(epoch, event["epoch"])
            sequence = max(sequence, event["seq"])
            tracking[key] = event
            continue
        if event["epoch"] != epoch:
            errors["view/submission uses an inactive tracking space"] += 1
        source = tracking.get(key)
        if source is None or source["predicted_ns"] != event["predicted_ns"]:
            errors["view/submission has no earlier matching prediction"] += 1
        frame_key = (event["epoch"], event.get("frame", 0))
        if kind == "view_consumed":
            eyes = views.setdefault(frame_key, {})
            if event["eye"] in eyes:
                errors["duplicate consumed eye"] += 1
            eyes[event["eye"]] = event
            continue
        submits += 1
        eyes = views.get(frame_key, {})
        if set(eyes) != {0, 1}:
            errors["submission lacks two verified consumed scene views"] += 1
            continue
        if any((view["epoch"], view["seq"], view["predicted_ns"]) !=
               (event["epoch"], event["seq"], event["predicted_ns"])
               for view in eyes.values()):
            errors["submitted pose differs from consumed view pose"] += 1
        if frame_key in submitted:
            repeats += 1
        else:
            submitted.add(frame_key)
        if "values" in eyes[0] and "values" in eyes[1] and \
                eyes[0]["values"] == eyes[1]["values"]:
            mono = True
    if not submits and domain != "combat":
        errors["no submissions captured"] += 1
    coverage_only = {"no submissions captured",
                     "submission lacks two verified consumed scene views"}
    correspondence = "PASS" if not errors else (
        "UNVERIFIED" if set(errors) <= coverage_only else "FAIL")
    checks = {
        "tracking_correspondence": {"status": correspondence,
                                    "detail": "Trace tags only; not GPU ownership."},
        "submission_coverage": {
            "status": "NOT_APPLICABLE" if domain == "combat" else
                      "PASS" if submits else "UNVERIFIED",
            "detail": "Combat-domain captures suspend XR submission recording."
                      if domain == "combat" else
                      "Submission counts are trace claims, not GPU ownership."},
        "gpu_stereo": {"status": "FAIL" if mono else "UNVERIFIED",
                       "detail": "Identical eye view matrices recorded." if mono else
                       "Verified GPU/image and runtime eye-geometry adapter absent."},
        "native_scenario": {"status": "UNVERIFIED",
                            "detail": "Native scenario execution is not recorded."},
        "comfort": {"status": "UNVERIFIED", "detail": "No human headset verdict."},
    }
    return {"complete": False, "valid": not errors, "errors": list(errors),
            "error_counts": dict(errors), "submissions": submits,
            "new_source_frames": len(submitted),
            "repeated_source_submissions": repeats,
            "source_counts_are_trace_claims": True, "checks": checks}


def _load_capture_records(path):
    records, malformed, parsed_records = [], [], []
    for line_number, raw_line in enumerate(path.read_bytes().splitlines(), 1):
        try:
            line = raw_line.decode("utf-8-sig")
        except UnicodeError:
            malformed.append(line_number)
            continue
        if not line.strip():
            continue
        try:
            record = json.loads(line)
        except ValueError:
            malformed.append(line_number)
            continue
        if isinstance(record, dict):
            parsed_records.append(record)
        if _record_ok(record):
            records.append(record)
        else:
            malformed.append(line_number)
    return records, malformed, parsed_records


def _read_capture_with_records(path, cases_path=None):
    """Analyse one capture. `cases_path` defaults to combat-cases.json beside it."""
    records, malformed, parsed_records = _load_capture_records(path)
    report = validate(records)
    report["malformed_lines"] = malformed
    headers = [e for e in records if e.get("event") == "header"]
    footers = [e for e in records if e.get("event") == "footer"]
    integrity = []
    if len(headers) != 1 or type(headers[0].get("schema")) is not int or \
            headers[0]["schema"] not in (1, 2) or \
            not _integer(headers[0].get("qpc_hz"), 1) or \
            records[0].get("event") != "header":
        integrity.append("capture has no unique supported header")
    if len(footers) != 1 or not records or records[-1].get("event") != "footer":
        integrity.append("capture has no closing footer")
    elif headers and (footers[0]["qpc"] < headers[0]["qpc"] or
                     any(e["qpc"] < headers[0]["qpc"] or
                         e["qpc"] > footers[0]["qpc"] for e in records)):
        integrity.append("capture records lie outside header/footer window")
    if len(headers) == 1 and headers[0].get("schema") == 2 and len(footers) == 1:
        footer = footers[0]
        if type(footer.get("interrupted")) is not bool or \
                type(footer.get("io_error")) is not bool or \
                not _integer(footer.get("events")) or \
                footer["events"] != len(records) - 2:
            integrity.append("capture completion acknowledgement is invalid")
    for footer in footers:
        if footer["dropped"] or footer.get("interrupted") or footer.get("io_error"):
            integrity.append("capture did not finish successfully")
    if malformed:
        integrity.append("capture has malformed records")
    report["errors"].extend(integrity)
    for error in integrity:
        report["error_counts"][error] = report["error_counts"].get(error, 0) + 1
    report["valid"] = report["valid"] and not integrity
    report["checks"]["capture_integrity"] = {
        "status": "FAIL" if integrity else "PASS", "detail": integrity}
    report["event_counts"] = dict(Counter(e.get("event") for e in records))
    report["capture_domain"] = headers[0].get("capture_domain", "legacy-unspecified") if len(headers) == 1 else None
    native_states = [e for e in records if e["event"] == "native_input_blockers"]
    masks = [sum(int(v) << (16 * i) for i, v in enumerate(e["values"][:4]))
             for e in native_states]
    report["native_readiness_observations"] = {
        "status": "UNVERIFIED", "samples": len(masks),
        "zero_mask_samples": masks.count(0),
        "unknown_owner_samples": sum(e["flags"] == 0 for e in native_states),
        "mask_counts": dict(Counter(f"{mask:016x}" for mask in masks)),
        "detail": "Raw native input blockers only. Zero does not prove safe gameplay, "
                  "scene identity, save isolation or movement authorization."}
    steps = [e for e in records if e["event"] == "native_step_dispatch"]
    transitions = [f'{e["flags"]}:{_signed_state(e["values"][:2])}->'
                   f'{_signed_state(e["values"][2:4])}' for e in steps
                   if e["values"][5] == 3]
    report["native_step_observations"] = {
        "status": "UNVERIFIED", "samples": len(steps),
        "unknown_state_samples": sum(e["values"][5] != 3 for e in steps),
        "kind_counts": dict(Counter(str(e["flags"]) for e in steps)),
        "transition_counts": dict(Counter(transitions)),
        "detail": "Raw Step dispatch states only. Active task topology, native scene "
                  "epoch, load/death completion and safe location are not verified."}
    report["periods_ns"] = sorted({e.get("period_ns", 0) for e in records
                                   if e.get("event") == "tracking"})
    report["measurements"] = {}
    frequency = headers[0].get("qpc_hz", 0) if len(headers) == 1 else 0
    frequency = frequency if type(frequency) is int and frequency > 0 else 0
    if cases_path is None:
        candidate = path.parent / "combat-cases.json"
        cases_path = candidate if candidate.is_file() else None
    cases, case_errors, declared = (), [], {}
    if cases_path is not None:
        cases, case_errors, declared = _case_plan(
            Path(cases_path), headers[0] if len(headers) == 1 else {})
        case_errors = list(case_errors)
    health_header = headers[0] if len(headers) == 1 else {}
    combat = _combat_observations(records,
        len(headers) == 1 and headers[0].get("combat_detail_schema") == 1, frequency, cases,
        len(headers) == 1 and headers[0].get("combat_context_schema") == 1,
        health_header.get("combat_health_schema", 0), health_header.get("combat_health_hook", 0))
    report["native_combat_observations"] = combat
    combat["callback_counts"] = {
        field: footers[0].get(field) if len(footers) == 1 else None
        for field in ("combat_total", "combat_non_player", "combat_self",
                      "combat_refused", "combat_detail", "combat_detail_limit")}
    activations = [e for e in records if e["event"] == "native_interaction_activation"]
    report["native_interaction_observations"] = {
        "status": "UNVERIFIED", "activation_callbacks": len(activations), "samples": activations,
        "detail": "Natural object-action requests only. Raw handles, state, PARAM distance and "
                  "return bits do not certify eligibility, prompt visibility, LOS, completion "
                  "or physical interaction. Handles have no qualified cross-callback generation."}
    span = ([headers[0]["qpc"], footers[0]["qpc"]] if len(headers) == 1 and len(footers) == 1
            and _integer(headers[0].get("qpc")) and _integer(footers[0].get("qpc")) else None)
    coverage = _case_coverage(cases, declared, span, frequency)
    for label, row in coverage.items():
        observed = combat["case_observations"].get(label)
        if observed is not None:
            observed["capture_overlap_ms"] = row["capture_overlap_ms"]
            observed["capture_covers_window"] = row["capture_covers_window"]
            observed["window_truncated"] = row["truncated"]
    # "No overlap" and "overlap unknown" are different facts: an unclosed capture has
    # no span, so its windows are unverified rather than unrecorded. Comparing to 0
    # and to False keeps None out of both accusation lists.
    unobserved = [label for label, row in coverage.items() if row["capture_overlap_ms"] == 0]
    uncovered = [label for label, row in coverage.items()
                 if row["capture_covers_window"] is False]
    # "Not fully covered" and "never recorded here" are different gaps: the first
    # leaves a floor on the sample count, the second leaves no observation at all.
    partly_covered = [label for label, row in coverage.items()
                      if row["capture_overlap_ms"] and not row["capture_covers_window"]]
    truncated = [label for label, row in coverage.items() if row["truncated"]]
    if not cases:
        coverage_status = "NOT_APPLICABLE"
    elif span is None:
        coverage_status = "UNVERIFIED"
    elif not uncovered:
        coverage_status = "PASS"
    else:
        coverage_status = "PARTIAL" if len(unobserved) < len(cases) else "NONE"
    report["combat_case_plan"] = {
        "status": "UNVERIFIED" if cases else "NOT_APPLICABLE",
        "path": str(cases_path) if cases_path else None, "windows": len(cases),
        "labels": [label for label, _, _ in cases], "errors": case_errors,
        "capture_span_qpc": span, "coverage": coverage, "coverage_status": coverage_status,
        "uncovered_labels": uncovered, "unobserved_labels": unobserved,
        "partly_covered_labels": partly_covered,
        "truncated_labels": truncated,
        "detail": "Operator-declared time windows on the capture QPC clock. A label says "
                  "what the operator intended in that window, not what the native game did; "
                  "only sample counts and observed fields are evidence."}
    report["checks"]["combat_case_coverage"] = {
        "status": coverage_status, "uncovered_labels": uncovered,
        "unobserved_labels": unobserved, "partly_covered_labels": partly_covered,
        "truncated_labels": truncated,
        "detail": "Whether this capture's header/footer span overlaps each declared window. "
                  "A window with no overlap has no observation at all, so its zero sample "
                  "count is a missing capture, not a negative result about the action."}
    bench = [e for e in records if e["event"] == "combat_bench_provisional"]
    report["combat_bench_observations"] = {
        "status": "UNVERIFIED", "provisional_samples": len(bench),
        "classification_frames": {name: sum(bool(e["flags"] & bit) for e in bench)
                                  for name, bit in (("geometry_hit", 1), ("block", 2),
                                                    ("parry", 4), ("hands_close", 8))},
        "max_tip_speed_mps": [max((e["values"][hand] for e in bench), default=None)
                              for hand in (0, 1)],
        "detail": "Provisional pre-copy bench geometry only, with assumed .9 m blade "
                  "and synthetic targets/threats. No confirmed native contact, damage, "
                  "block, parry or final rendered weapon is established."}
    combat_errors = list(combat["pairing_errors"]) + case_errors
    if integrity:
        combat_errors.append("capture integrity failed; combat observations are incomplete")
    report["checks"]["combat_observation_integrity"] = {
        "status": "FAIL" if combat_errors else "PASS" if combat["diagnostic_samples"] else "UNVERIFIED",
        "detail": combat_errors}
    if combat["pairing_errors"]:
        report["errors"].extend(combat["pairing_errors"])
        report["valid"] = False
        report["error_counts"]["combat observation pairing failed"] = len(combat["pairing_errors"])
    # A requested case plan that cannot be trusted is a refusal, not a hint: the
    # alternative is labelling samples with windows on the wrong clock.
    if case_errors:
        report["errors"].extend(case_errors)
        report["valid"] = False
        report["error_counts"]["case plan rejected"] = len(case_errors)
    combat["coverage_limitations"] = [
        "Only natural callbacks completing within the capture are observed; simulation/contact producer identity is unverified.",
        "Health-setter calls are recorded only while an admitted detailed contact callback is on the stack. A missing setter record does not rule out an asynchronous or alternate HP path, and a recorded request is not proof that HP changed or that the contact caused it.",
        "Detail admission follows the recorded capture limit when present; legacy captures "
        "may use a 256-call ceiling. Absence of an HP delta does not prove a miss or block."]
    if cases:
        combat["coverage_limitations"].append(
            "Case labels bind by QPC window only. A window with no samples proves nothing "
            "about the declared action, and no native outcome is labelled.")
        combat["coverage_limitations"].append(
            "Capture coverage is per file: a declared window this capture does not overlap "
            "was never recorded here, so zero samples in it says nothing about the declared "
            "action. Only another capture overlapping the same window can settle it.")
        if span is None:
            combat["coverage_limitations"].append(
                "This capture never wrote its closing footer, so its span is unknown and "
                "no window's coverage can be judged here: the gap is unverified, not "
                "zero. Only a capture that closed can settle whether a window was "
                "recorded.")
        if partly_covered:
            combat["coverage_limitations"].append(
                "Declared windows this capture overlaps but does not cover end to end: " +
                ", ".join(partly_covered) +
                ". A 20 s capture cannot cover a 45 s window, so its sample count is a "
                "floor, not the whole window.")
        if unobserved:
            combat["coverage_limitations"].append(
                "Declared windows with no capture overlap at all: " + ", ".join(unobserved) +
                ". Zero samples there is a missing capture, not a negative result.")
        if truncated:
            combat["coverage_limitations"].append(
                "Windows cut short before their declared duration: " + ", ".join(truncated) +
                ". A short window is a partial opportunity, not a completed attempt.")
        combat["coverage_limitations"].append(
            "case_attribution is temporal proximity on one clock, not causation: a tracked "
            "classification near a native callback does not make it the cause, the tracked "
            "side still uses assumed .9 m blade geometry and synthetic targets, and zero "
            "bench samples means no tracked evidence existed, not that no swing occurred.")
    if combat["contact_limit_reached"]:
        combat["coverage_limitations"].append("Detail limit reached; later callbacks have no actor reads.")
    if integrity or combat_errors:
        combat["coverage_limitations"].append("Capture loss or incomplete/ambiguous samples prevent complete correlation.")
    report["queue_health"] = {field: footers[0].get(field) if len(footers) == 1 else None
                              for field in ("dropped", "queue_peak", "contended")}
    if type(frequency) is int and frequency > 0:
        for kind in ("native_wait", "render_exit", "present_exit"):
            values = [e["duration_qpc"] for e in records
                             if e.get("event") == kind and
                             type(e.get("duration_qpc")) is int and e["duration_qpc"] >= 0]
            if values:
                report["measurements"][kind] = _timing_summary(values, frequency)
    # Motion here means movement of the WRITTEN camera, not proof of character
    # locomotion or a fusion verdict. It stops idle captures posing as gait data.
    positions = [e.get("values", [])[12:15] for e in records
                 if e.get("event") == "camera_applied"]
    positions = [p for p in positions if len(p) == 3 and
                 all(type(v) in (int, float) and math.isfinite(v) for v in p)]
    report["camera_motion_recorded"] = bool(positions) and any(
        max(p[i] for p in positions) - min(p[i] for p in positions) > .001
        for i in range(3))
    report["limitations"] = ["Camera command production is not GPU/view consumption.",
                            "Written camera motion is not native scenario execution.",
                            "Physical binocular fusion requires a headset verdict."]
    return report, parsed_records

def read_capture(path, cases_path=None):
    """Analyse one capture; cases_path defaults to combat-cases.json beside it."""
    report, _ = _read_capture_with_records(path, cases_path)
    return report


def _is_session_index(path):
    try:
        for raw in path.read_bytes().splitlines():
            line = raw.decode("utf-8-sig").strip()
            if not line:
                continue
            try:
                record = json.loads(line)
                if not isinstance(record, dict):
                    continue
                return record.get("event") == "session_start"
            except ValueError:
                return False
    except (OSError, UnicodeError):
        return False
    return False


def _session_lifecycle_record_valid(record):
    kind = record["event"]
    if not _integer(record.get("qpc")):
        return False
    if "utc_filetime_100ns" in record and not _integer(record["utc_filetime_100ns"]):
        return False
    if kind == "session_start" and any(not _integer(record.get(field), 1)
                                        for field in ("pid", "qpc_hz")):
        return False
    if kind in ("epoch_start", "epoch_end", "segment_start", "segment_end",
                "segment_failure", "capture_stopped") and not _integer(record.get("epoch"), 1):
        return False
    if kind in ("segment_start", "segment_end", "segment_failure") and \
            not _integer(record.get("segment"), 1):
        return False
    if kind == "segment_start":
        leaf = record.get("segment_file")
        if not isinstance(leaf, str) or not leaf or leaf in (".", "..") or \
                any(char in leaf for char in ("/", "\\", "\0")):
            return False
        if "gap_qpc" in record and (type(record["gap_qpc"]) is not int or
                                    not -(1 << 63) <= record["gap_qpc"] < (1 << 63)):
            return False
    if kind in ("epoch_end", "capture_stopped") and "reason" not in record:
        return False
    if "reason" in record and (not isinstance(record["reason"], str) or not record["reason"]):
        return False
    for field in ("duration_qpc", "capture_id", "events", "dropped", "contended", "queue_peak"):
        if field in record and not _integer(record[field]):
            return False
    if "io_error" in record and type(record["io_error"]) is not bool:
        return False
    if "degraded" in record and type(record["degraded"]) is not bool:
        return False
    if "event_counts" in record:
        counts = record["event_counts"]
        if not isinstance(counts, dict) or any(not isinstance(name, str) or not name or
                                               not _integer(count) for name, count in counts.items()):
            return False
    return True


def analyze_session_index(path):
    """Validate one autonomous-session index plus its segment captures.

    The index binds session/epoch/segment identity; every segment file must
    still close with its own producer footer. Anything less is incomplete,
    never a negative result about the game.
    """
    errors = []
    try:
        text = path.read_text(encoding="utf-8-sig")
    except (OSError, UnicodeError) as error:
        return dict(report_type="autonomous_vr_session", valid=False,
                    complete=False, errors=[str(error)])
    index_records = []
    for line in text.splitlines():
        if not line.strip():
            continue
        try:
            record = json.loads(line)
            if not isinstance(record, dict) or not isinstance(record.get("event"), str) or not record["event"]:
                errors.append("session index has malformed records")
                continue
            index_records.append(record)
        except ValueError:
            errors.append("session index has malformed records")
    starts = [r for r in index_records if r.get("event") == "session_start"]
    if len(starts) != 1:
        errors.append("session index has no unique session_start")
    session = starts[0] if len(starts) == 1 else {}
    session_id = session.get("session_id")
    lifecycle_events = {"session_start", "session_end", "epoch_start", "epoch_end",
                        "segment_start", "segment_end", "segment_failure", "capture_stopped"}
    session_records = []
    if not isinstance(session_id, str) or not session_id:
        errors.append("session_start has no valid session_id")
    else:
        for record in index_records:
            if record.get("event") not in lifecycle_events:
                continue
            if record.get("session_id") != session_id:
                errors.append("session index contains foreign lifecycle records")
                continue
            if not _session_lifecycle_record_valid(record):
                errors.append("session index has malformed %s record" % record["event"])
                continue
            session_records.append(record)
    previous_qpc = None
    for record in session_records:
        qpc = record["qpc"]
        if previous_qpc is not None and qpc < previous_qpc:
            errors.append("session index QPC regressed from %s to %s at %s" %
                          (previous_qpc, qpc, record["event"]))
        previous_qpc = qpc
    for event, fields in (("epoch_start", ("epoch",)), ("epoch_end", ("epoch",)),
                          ("segment_end", ("epoch", "segment"))):
        seen = set()
        for record in session_records:
            if record["event"] != event:
                continue
            key = tuple(record[field] for field in fields)
            if key in seen:
                errors.append("session index has duplicate %s record %s" % (event, key))
            seen.add(key)
    seg_starts = [r for r in session_records if r.get("event") == "segment_start"]
    if not seg_starts:
        errors.append("session index has no captured segments")
    segment_end_records = {(r.get("epoch"), r.get("segment")): r
                           for r in session_records if r.get("event") == "segment_end"}
    seg_ends = set(segment_end_records)
    for (epoch, segment), record in segment_end_records.items():
        if record.get("io_error"):
            errors.append("session segment %s epoch %s index io_error" % (segment, epoch))
    epoch_start_records = {r.get("epoch"): r for r in session_records
                           if r.get("event") == "epoch_start"}
    epoch_end_records = {r.get("epoch"): r for r in session_records
                         if r.get("event") == "epoch_end"}
    epoch_starts = set(epoch_start_records)
    epoch_ends = set(epoch_end_records)
    for epoch in epoch_ends - epoch_starts:
        errors.append("session epoch %s has no epoch_start" % (epoch,))
    for epoch in epoch_starts & epoch_ends:
        if epoch_end_records[epoch]["qpc"] < epoch_start_records[epoch]["qpc"]:
            errors.append("session epoch %s ends before it starts" % (epoch,))
    for epoch, record in epoch_end_records.items():
        if record.get("degraded"):
            errors.append("session epoch %s degraded" % (epoch,))
    session_end_records = [r for r in session_records
                           if r.get("event") == "session_end"]
    capture_stops = [r for r in session_records
                     if r.get("event") == "capture_stopped"]
    stop_reasons = {r.get("reason") or "unspecified" for r in capture_stops}
    stop_reasons.update(r.get("reason") for r in session_records
                        if r.get("event") == "epoch_end" and r.get("reason")
                        and r.get("reason") != "player_unload")
    if not epoch_starts:
        errors.append("session index has no epoch boundaries")
    if not session_end_records:
        errors.append("session has no session_end")
    elif len(session_end_records) != 1:
        errors.append("session index has duplicate session_end records")
    gaps = []
    dropped = contended = 0
    pad_changes = valid_hands = hand_transitions = 0
    previous_footers = {}
    seen_epochs = set()
    seen_segments = set()
    event_counts = Counter()
    for start in seg_starts:
        epoch, segment = start.get("epoch"), start.get("segment")
        key = (epoch, segment)
        if key in seen_segments:
            errors.append("session segment %s epoch %s has duplicate starts" % (segment, epoch))
            continue
        seen_segments.add(key)
        same_epoch = epoch in seen_epochs
        seen_epochs.add(epoch)
        segment_end = segment_end_records.get(key)
        epoch_start = epoch_start_records.get(epoch)
        epoch_end = epoch_end_records.get(epoch)
        if epoch_start is None:
            errors.append("session segment %s epoch %s has no epoch_start" % (segment, epoch))
        elif start["qpc"] < epoch_start["qpc"]:
            errors.append("session segment %s starts before epoch %s" % (segment, epoch))
        if epoch_end is None:
            errors.append("session segment %s epoch %s has no epoch_end" % (segment, epoch))
        elif start["qpc"] > epoch_end["qpc"] or (
                segment_end is not None and segment_end["qpc"] > epoch_end["qpc"]):
            errors.append("session segment %s ends outside epoch %s" % (segment, epoch))
        if segment_end is not None and segment_end["qpc"] < start["qpc"]:
            errors.append("session segment %s ends before it starts" % (segment,))
        index_dropped = segment_end.get("dropped", 0) if segment_end else 0
        index_contended = segment_end.get("contended", 0) if segment_end else 0
        dropped += index_dropped
        contended += index_contended
        leaf = start.get("segment_file")
        seg_file = path.parent / (path.stem + "-" + leaf) if leaf else None
        if not leaf or seg_file is None or not seg_file.is_file():
            errors.append("session segment %s file missing" % (segment,))
            continue
        try:
            seg_report, seg_records = _read_capture_with_records(seg_file)
            seg_records = [record for record in seg_records if _record_ok(record)]
        except OSError as error:
            errors.append(str(error))
            continue
        for error in seg_report.get("errors", []):
            if error in ("no submissions captured",
                         "submission lacks two verified consumed scene views"):
                continue
            errors.append(error)
        segment_counts = seg_report.get("event_counts", {})
        event_counts.update(segment_counts)
        if segment_end and "event_counts" in segment_end and segment_end.get("io_error") is not True:
            if segment_end["event_counts"] != segment_counts:
                errors.append("session segment %s event counts mismatch" % (segment,))
        headers = [e for e in seg_records if e.get("event") == "header"]
        footers = [e for e in seg_records if e.get("event") == "footer"]
        if len(headers) != 1:
            errors.append("session segment %s has no unique header" % (segment,))
            continue
        header = headers[0]
        for field, want in (("pid", session.get("pid")),
                            ("qpc_hz", session.get("qpc_hz")),
                            ("capture_domain", "session"),
                            ("session_id", session_id),
                            ("player_epoch", epoch),
                            ("segment_index", segment)):
            if header.get(field) != want:
                errors.append("session segment %s header %s mismatch" % (segment, field))
        if not footers or seg_records[-1].get("event") != "footer":
            errors.append("session segment %s has no closing footer" % (segment,))
        else:
            footer = footers[0]
            dropped += max(0, footer.get("dropped", 0) - index_dropped)
            contended += max(0, footer.get("contended", 0) - index_contended)
            if segment_end and segment_end.get("io_error") is not True:
                for field in ("dropped", "contended"):
                    if field in segment_end and segment_end[field] != footer.get(field, 0):
                        errors.append("session segment %s %s mismatch" % (segment, field))
            previous_footer = previous_footers.get(epoch)
            if previous_footer is not None:
                gap = header["qpc"] - previous_footer
                if gap:
                    gaps.append({"segment": segment, "gap_qpc": gap})
            previous_footers[epoch] = footer["qpc"]
        declared = start.get("gap_qpc", 0)
        if same_epoch and declared:
            gaps.append({"segment": segment, "gap_qpc": declared})
        if (epoch, segment) not in seg_ends:
            errors.append("session segment %s has no segment_end" % (segment,))
        for e in seg_records:
            if e.get("event") == "virtual_pad_state":
                pad_changes += 1
            elif e.get("event") == "hand_tracking":
                flags = e.get("flags", 0)
                if type(flags) is not int:
                    errors.append("session segment %s malformed hand tracking flags" % (segment,))
                    continue
                if flags & 1:
                    valid_hands += 1
                if flags & 2:
                    hand_transitions += 1
    for epoch, segment in seg_ends - seen_segments:
        errors.append("session segment %s epoch %s has no segment_start" % (segment, epoch))
    empty_epochs = sorted(epoch_starts - seen_epochs)
    if empty_epochs:
        errors.extend("session epoch %s has no captured segments" % (epoch,)
                      for epoch in empty_epochs)
    open_epochs = sorted(e for e in epoch_starts if e not in epoch_ends)
    continuous = not gaps
    complete = not errors and continuous and not open_epochs and not dropped and not contended and not stop_reasons
    session_end = session_end_records[-1] if session_end_records else {}
    utc_bounds = {
        "session": {
            "qpc_start": session.get("qpc"),
            "qpc_end": session_end.get("qpc"),
            "utc_start_filetime_100ns": session.get("utc_filetime_100ns"),
            "utc_end_filetime_100ns": session_end.get("utc_filetime_100ns"),
        },
        "epochs": [
            {"epoch": start.get("epoch"), "qpc_start": start.get("qpc"),
             "qpc_end": epoch_end_records.get(start.get("epoch"), {}).get("qpc"),
             "utc_start_filetime_100ns": start.get("utc_filetime_100ns"),
             "utc_end_filetime_100ns": epoch_end_records.get(start.get("epoch"), {}).get("utc_filetime_100ns")}
            for start in session_records if start.get("event") == "epoch_start"],
        "segments": [
            {"epoch": start.get("epoch"), "segment": start.get("segment"),
             "qpc_start": start.get("qpc"),
             "qpc_end": segment_end_records.get((start.get("epoch"), start.get("segment")), {}).get("qpc"),
             "utc_start_filetime_100ns": start.get("utc_filetime_100ns"),
             "utc_end_filetime_100ns": segment_end_records.get((start.get("epoch"), start.get("segment")), {}).get("utc_filetime_100ns")}
            for start in seg_starts],
    }
    return dict(report_type="autonomous_vr_session",
                valid=not errors, complete=complete, acceptance="UNVERIFIED",
                unknowns={"scene_identity": "unknown"},
                input_observations={
                    "virtual_pad_state_changes": pad_changes,
                    "valid_hand_samples": valid_hands,
                    "hand_validity_or_space_transitions": hand_transitions,
                    "detail": "Change-gated pad states and <=30 Hz hand samples. "
                              "Raw input only: no intent, gesture, or menu/dialogue "
                              "selection is established."},
                coverage={"continuous": continuous, "gaps": gaps,
                          "open_epochs": open_epochs},
                loss={"dropped": dropped, "contended": contended}, event_counts=dict(event_counts),
                utc_bounds=utc_bounds, stop_reasons=sorted(stop_reasons),
                errors=errors)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path)
    parser.add_argument("--cases", type=Path,
                        help="operator case windows; auto-detected beside the capture")
    args = parser.parse_args()
    try:
        report = (analyze_session_index(args.path) if _is_session_index(args.path)
                else read_capture(args.path, args.cases))
    except OSError as error:
        print(json.dumps(dict(complete=False, errors=[str(error)]), indent=2))
        return 2
    print(json.dumps(report, indent=2))
    return 0 if report["complete"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
