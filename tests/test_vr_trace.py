"""Approved trace/replay seam: synthetic provenance is not live proof."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from vr_trace import validate, read_capture


def check(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    events = [
        dict(event="tracking", qpc=10, seq=7, epoch=1, predicted_ns=900),
        dict(event="view_consumed", qpc=20, seq=7, epoch=1, frame=11, eye=0,
             predicted_ns=900),
        dict(event="view_consumed", qpc=21, seq=7, epoch=1, frame=11, eye=1,
             predicted_ns=900),
        dict(event="submit", qpc=30, seq=7, epoch=1, frame=11, predicted_ns=900),
    ]
    good = validate(events)
    check(good["valid"] and not good["errors"], "matching correspondence fixture rejected")
    check(not good["complete"], "trace tags alone certified runtime VR")
    check(good["checks"]["gpu_stereo"]["status"] == "UNVERIFIED",
          "missing GPU adapter hidden by green tags")
    check(good["checks"]["comfort"]["status"] == "UNVERIFIED",
          "automated fixture certified human comfort")
    zero = [dict(e, seq=0, epoch=0) for e in events]
    check(not validate(zero)["valid"], "zero identities accepted")
    missing = [dict(e) for e in events]
    del missing[-1]["predicted_ns"]
    check(not validate(missing)["valid"], "missing prediction accepted or crashed")
    wrong_prediction = [dict(e, predicted_ns=1000) if e["event"] == "view_consumed"
                        else dict(e) for e in events]
    check(not validate(wrong_prediction)["valid"], "consumed prediction mismatch accepted")
    repeated = validate(events + [dict(events[-1], qpc=31)])
    check(repeated["valid"] and repeated["new_source_frames"] == 1 and
          repeated["repeated_source_submissions"] == 1, "repeats counted as new renders")
    identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
    mono = [dict(e, values=identity) if e["event"] == "view_consumed"
            else dict(e) for e in events]
    check(validate(mono)["checks"]["gpu_stereo"]["status"] == "FAIL",
          "identical eye views not diagnosed as mono")
    epoch_change = events[:3] + [dict(events[0], qpc=25, seq=8, epoch=2)] + events[3:]
    check(not validate(epoch_change)["valid"], "old space submitted after epoch change")
    recycled = events + [dict(events[0], qpc=40, seq=8, epoch=2)] + [
        dict(e, qpc=e["qpc"] + 30, seq=8, epoch=2) for e in events[1:]]
    check(validate(recycled)["new_source_frames"] == 2, "source IDs collided across spaces")
    malformed = [None, [], True, {}, {"event": "tracking", "qpc": True},
                 dict(events[0], seq=[]), dict(events[0], predicted_ns=float("nan")),
                 dict(events[0], qpc=10**400), dict(events[1], eye=True),
                 dict(events[1], values=[float("nan")] * 16)]
    for record in malformed:
        result = validate([record])
        check(not result["valid"] and "malformed event" in result["errors"],
              "malformed record accepted or crashed")
    delayed = [dict(e) for e in events]
    delayed[-1].update(seq=8, predicted_ns=1000)
    bad = validate(delayed)
    check(not bad["valid"] and bad["errors"], "one-packet delay must go red")
    # Native command-production tags are explicitly weaker evidence. Even
    # matching matrices and poses cannot pass without actual view consumption.
    partial = [events[0], dict(event="camera_command", qpc=20, seq=7), events[-1]]
    check(not validate(partial)["valid"], "producer-only capture falsely accepted")
    duplicate = [events[0], events[1], dict(events[1], qpc=21), events[-1]]
    check(not validate(duplicate)["valid"], "duplicate left eye counted as stereo")
    dropped = events + [dict(event="footer", qpc=40, dropped=1)]
    check(not validate(dropped)["valid"], "capture with missing events accepted")
    host_source = dict(event="host_source", qpc=25, frame=1, object="2a",
                       flags=2, values=[1, 0, 0, 0] + [0] * 12)
    check(validate(events + [host_source])["valid"], "host source rejected")
    check(not validate([events[0], host_source, events[-1]])["valid"],
          "resource observation substituted for consumed views")
    for kind in ("host_source", "host_swapchain", "host_device"):
        for fields in (dict(frame=0), dict(flags=True), dict(flags=3),
                       dict(object="0"), dict(epoch=1), dict(seq=1),
                       dict(predicted_ns=900), dict(values=[0] * 16),
                       dict(values=[1.5] + [0] * 15)):
            check(not validate(events + [dict(host_source, event=kind, **fields)])["valid"],
                  "malformed host source or fabricated view identity accepted")
    native_state = dict(event="native_input_blockers", qpc=25, seq=1,
                        object="2a", flags=1, values=[1, 0, 0, 0] + [0] * 12)
    check(validate(events + [native_state])["valid"], "raw native mask rejected")
    for value in (-1, 65536, 1.5, True):
        corrupt = dict(native_state, values=[value, 0, 0, 0] + [0] * 12)
        check(not validate(events + [corrupt])["valid"], "corrupt mask limb accepted")
    for fields in (dict(seq=0), dict(flags=True), dict(object="not-an-address"),
                   dict(object="0"), dict(values=None)):
        check(not validate(events + [dict(native_state, **fields)])["valid"],
              "missing or contradictory native identity accepted")
    native_step = dict(event="native_step_dispatch", qpc=25, seq=1, object="2a",
                       flags=3, duration_qpc=0, values=[0, 0, 1, 0, .125, 3] + [0] * 10)
    check(validate(events + [native_step])["valid"], "raw native step rejected")
    for field, value in (("seq", 0), ("flags", True), ("flags", 4),
                         ("object", "0"), ("object", "1" * 17),
                         ("duration_qpc", None), ("values", None)):
        check(not validate(events + [dict(native_step, **{field: value})])["valid"],
              "malformed native step identity accepted")
    for index, value in ((0, -1), (1, 65536), (2, .5), (3, True),
                         (4, float("nan")), (5, 4), (5, 1.5)):
        values = list(native_step["values"])
        values[index] = value
        check(not validate(events + [dict(native_step, values=values)])["valid"],
              "corrupt native step payload accepted")
    # A process can close during a buffered write. Preserve usable observations
    # but mark missing footer / truncated JSON as incomplete, not success.
    import json
    import tempfile
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "capture.jsonl"
        path.write_text(json.dumps(dict(event="header", qpc=1, schema=1, qpc_hz=1000)) +
                        "\n" + "\n".join(json.dumps(e) for e in events) +
                        '\n{"event":"present_exit","qpc":', encoding="utf-8")
        capture = read_capture(path)
        check(not capture["complete"], "truncated capture accepted")
        check(capture["malformed_lines"] == [6], "usable prefix lost")
        check("capture has no closing footer" in capture["errors"], "missing footer unnoticed")
        header = dict(event="header", qpc=1, schema=2, qpc_hz=1000)
        footer = dict(event="footer", qpc=40, dropped=0, events=4,
                      interrupted=False, io_error=False)
        def capture_report(records):
            path.write_text("\n".join(json.dumps(e) for e in records), encoding="utf-8")
            return read_capture(path)
        closed = [header] + events + [footer]
        report = capture_report(closed)
        check(report["valid"] and not report["complete"], "closed tags certified runtime VR")
        check(report["checks"]["capture_integrity"]["status"] == "PASS",
              "closed valid capture rejected")
        for field in ("interrupted", "io_error"):
            report = capture_report(closed[:-1] + [dict(footer, **{field: True})])
            check(not report["valid"] and
                  report["checks"]["capture_integrity"]["status"] == "FAIL",
                  "failed writer acknowledgement accepted")
        bad_count = capture_report(closed[:-1] + [dict(footer, events=3)])
        check(not bad_count["valid"], "missing written event count unnoticed")
        bad_step = capture_report([header] + events + [dict(native_step, flags=4)] +
                                  [dict(footer, events=5)])
        check(bad_step["malformed_lines"] == [6] and
              bad_step["checks"]["capture_integrity"]["status"] == "FAIL" and
              bad_step["native_step_observations"]["status"] == "UNVERIFIED" and
              bad_step["submissions"] == 1,
              "malformed step crashed reader, erased usable prefix, or granted readiness")
        invalid_clock = capture_report([dict(header, qpc_hz=0)] + events + [footer])
        check(not invalid_clock["valid"], "invalid clock calibration accepted")
        late_record = capture_report(closed[:-1] + [dict(footer, qpc=25)])
        check(not late_record["valid"], "events after closing timestamp accepted")
        path.write_bytes(b'\xff\n' + json.dumps(header).encode("utf-8"))
        invalid_utf8 = read_capture(path)
        check(invalid_utf8["malformed_lines"] == [1] and not invalid_utf8["valid"],
              "invalid UTF-8 accepted or crashed")
        if len(sys.argv) > 1:
            import subprocess
            path.unlink()  # remove only the file this test created
            subprocess.run([sys.argv[1], "--fixture", str(path)], check=True,
                           timeout=10, capture_output=True)
            actual = read_capture(path)
            check(actual["checks"]["capture_integrity"]["status"] == "PASS",
                  "production writer output rejected by production reader")
            check(not actual["complete"] and actual["event_counts"]["test_observation"] == 1,
                  "writer round-trip invented runtime acceptance")
            path.unlink()  # this test's file only; writer exclusively creates the next
            subprocess.run([sys.argv[1], "--readiness-fixture", str(path)], check=True,
                           timeout=10, capture_output=True)
            raw_state = read_capture(path)
            check(raw_state["checks"]["capture_integrity"]["status"] == "PASS",
                  "production native-state writer output rejected")
            state = raw_state["native_readiness_observations"]
            check(state["status"] == "UNVERIFIED" and not raw_state["complete"],
                  "raw blockers granted scene or VR acceptance")
            check(state["samples"] == 3 and state["zero_mask_samples"] == 1 and
                  state["unknown_owner_samples"] == 1, "raw state coverage lost")
            check(state["mask_counts"] == {"abcd800000000001": 1,
                  "0000000000000000": 1, "ffffffffffffffff": 1},
                  "64-bit mask rounded, truncated, or sign-extended")
            path.unlink()
            subprocess.run([sys.argv[1], "--step-fixture", str(path)], check=True,
                           timeout=10, capture_output=True)
            steps = read_capture(path)
            check(steps["checks"]["capture_integrity"]["status"] == "PASS",
                  "production step writer output rejected")
            lifecycle = steps["native_step_observations"]
            check(lifecycle["status"] == "UNVERIFIED" and not steps["complete"] and
                  steps["checks"]["native_scenario"]["status"] == "UNVERIFIED",
                  "raw native task states granted scene/scenario acceptance")
            check(lifecycle["samples"] == 3 and lifecycle["unknown_state_samples"] == 1,
                  "missing state read hidden or coverage lost")
            check(lifecycle["transition_counts"] == {"3:0->1": 1,
                  "2:2147483647->-2147483648": 1},
                  "signed states rounded or invalid state used in transition counts")
            check(lifecycle["kind_counts"] == {"3": 1, "2": 1, "1": 1},
                  "native task class observations conflated")
    print("test_vr_trace: checks passed")


if __name__ == "__main__":
    main()
