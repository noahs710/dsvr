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
    return True


def validate(events):
    """Check tag correspondence, NOT the truth of the claimed GPU observations.

    valid describes these checks only. complete remains false until separate
    native/GPU/comfort acceptance is available; arbitrary JSON cannot certify it.
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
    if not submits:
        errors["no submissions captured"] += 1
    coverage_only = {"no submissions captured",
                     "submission lacks two verified consumed scene views"}
    correspondence = "PASS" if not errors else (
        "UNVERIFIED" if set(errors) <= coverage_only else "FAIL")
    checks = {
        "tracking_correspondence": {"status": correspondence,
                                    "detail": "Trace tags only; not GPU ownership."},
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


def read_capture(path):
    records, malformed = [], []
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
            if not _record_ok(record):
                raise ValueError("event is not an object")
            records.append(record)
        except ValueError:
            malformed.append(line_number)
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
    if type(frequency) is int and frequency > 0:
        for kind in ("native_wait", "render_exit", "present_exit"):
            values = sorted(e["duration_qpc"] * 1000 / frequency for e in records
                            if e.get("event") == kind and
                            type(e.get("duration_qpc")) is int and e["duration_qpc"] >= 0)
            if values:
                report["measurements"][kind] = dict(count=len(values),
                    median_ms=statistics.median(values),
                    p95_ms=values[int((len(values) - 1) * .95)], max_ms=values[-1])
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
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path)
    args = parser.parse_args()
    try:
        report = read_capture(args.path)
    except OSError as error:
        print(json.dumps(dict(complete=False, errors=[str(error)]), indent=2))
        return 2
    print(json.dumps(report, indent=2))
    return 0 if report["complete"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
