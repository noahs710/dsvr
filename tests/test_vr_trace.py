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
    bench = dict(event="combat_bench_provisional", qpc=25, seq=1, epoch=1,
                 flags=5, values=[1.5, 2.5] + [0] * 14)
    check(validate(events + [bench])["valid"], "provisional bench trace rejected")
    for fields in (dict(seq=0), dict(epoch=0), dict(flags=True), dict(flags=16)):
        check(not validate(events + [dict(bench, **fields)])["valid"],
              "malformed provisional bench trace accepted")
    # The caller-built record's own direction/velocity fields. Every number is
    # gated by the read bit that produced it, so a value with no read behind it
    # is a producer bug and a unit-length claim must survive its own length.
    motion = dict(event="native_combat_motion", qpc=25, seq=1, thread=7,
                  object="1403c4020", flags=31, native_rate=0x12345678,
                  values=[0, 0, -1, 0, 0, 0, -4.5, 0, 1.0, 4.5] + [0] * 6)
    check(validate(events + [motion])["valid"], "native motion record rejected")
    partial = dict(motion, flags=5, values=[0.7, 0, 0, 0, 0, 0, 0, 0, 0.7, 0] +
                   [0] * 6)
    check(validate(events + [partial])["valid"],
          "partial motion read (direction only) rejected")
    for fields in (dict(seq=0), dict(thread=0), dict(flags=True), dict(flags=32),
                   dict(native_rate=0x100000000), dict(object="0"),
                   dict(object="0" * 17), dict(values=[0] * 16)):
        check(not validate(events + [dict(motion, **fields)])["valid"],
              "malformed native motion accepted")
    for index, value in ((0, 1.0), (4, 1.0), (8, 1.0), (9, 1.0), (10, 1.0)):
        values = list(motion["values"])
        values[index] = value
        check(not validate(events + [dict(motion, flags=0, values=values)])["valid"],
              "motion payload without its read bit accepted")
    check(not validate(events + [dict(motion, values=[0, 0, -1, 0, 0, 0, -4.5, 0,
                                      0.7, 4.5] + [0] * 6)])["valid"],
          "unit-length bit claimed for a non-unit direction")
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
        # A combat-domain capture suspends XR submission recording by design, so
        # an empty submission stream is its declared scope, never a defect.
        contact_only = [dict(event="tracking", qpc=10, seq=7, epoch=1,
                         predicted_ns=900),
                    dict(event="camera_applied", qpc=11, seq=7, epoch=1,
                         values=[0.0] * 16)]
        combat = capture_report([dict(header, capture_domain="combat", combat_hook=1)] +
                            contact_only + [dict(footer, events=2)])
        check(combat["valid"] and not combat["complete"] and
          combat["capture_domain"] == "combat" and combat["submissions"] == 0 and
          combat["checks"]["submission_coverage"]["status"] == "NOT_APPLICABLE" and
          "no submissions captured" not in combat["errors"],
          "combat-domain capture judged invalid for its declared scope")
        check(combat["checks"]["tracking_correspondence"]["status"] == "PASS" and
          combat["checks"]["gpu_stereo"]["status"] == "UNVERIFIED",
          "combat scope excused stereo or scenario acceptance")
        check(not capture_report([dict(header, capture_domain="full")] + contact_only +
                             [dict(footer, events=2)])["valid"],
          "full-domain capture excused for recording no submissions")
        check(not capture_report([dict(header, capture_domain="combat")] +
                             [contact_only[0], dict(events[-1])] +
                               [dict(footer, events=2)])["valid"],
          "combat capture with a submission lacking consumed views stayed green")
        for fields in (dict(combat_detail_schema=True), dict(combat_detail_schema=2),
                       dict(combat_hook=-1)):
            check(not capture_report([dict(header, **fields)] + events + [footer])["valid"],
                  "unknown combat header capability accepted")
        for fields in (dict(queue_peak=4097), dict(contended=1), dict(queue_peak=True)):
            check(not capture_report(closed[:-1] + [dict(footer, **fields)])["valid"],
                  "contradictory queue telemetry accepted")
        for field in ("interrupted", "io_error"):
            report = capture_report(closed[:-1] + [dict(footer, **{field: True})])
            check(not report["valid"] and
                  report["checks"]["capture_integrity"]["status"] == "FAIL",
                  "failed writer acknowledgement accepted")
        bad_count = capture_report(closed[:-1] + [dict(footer, events=3)])
        check(not bad_count["valid"], "missing written event count unnoticed")
        provisional = capture_report([header] + events + [bench] + [dict(footer, events=5)])
        check(provisional["combat_bench_observations"]["classification_frames"] ==
              {"geometry_hit": 1, "block": 0, "parry": 1, "hands_close": 0} and
              provisional["combat_bench_observations"]["status"] == "UNVERIFIED",
              "bench geometry promoted to native combat outcomes")
        bad_step = capture_report([header] + events + [dict(native_step, flags=4)] +
                                  [dict(footer, events=5)])
        check(bad_step["malformed_lines"] == [6] and
              bad_step["checks"]["capture_integrity"]["status"] == "FAIL" and
              bad_step["native_step_observations"]["status"] == "UNVERIFIED" and
              bad_step["submissions"] == 1,
              "malformed step crashed reader, erased usable prefix, or granted readiness")
        # Operator-declared case windows bind by QPC only. A label is a plan,
        # never an observed native outcome, and a mismatched clock must refuse
        # binding rather than silently select the wrong samples.
        def combat_case_sample(qpc, seq, hp_delta):
            shared = dict(qpc=qpc, seq=seq, thread=7, object="1403c4020",
                          flags=15, native_rate=0x12345678)
            return [dict(shared, event="native_combat_contact", duration_qpc=50,
                         values=[1, 0, 0, 0, 2, 0, 0, 0, 1000, 1000, 120, 120,
                                 400, 400 + hp_delta, 100, 100]),
                    dict(shared, event="native_combat_identity",
                         values=[1, 0, 0, 0, 2, 0, 0, 0, 0x5678, 0x1234,
                                 0xabcd, 0, 0x5678, 0x1234, 0, 0]),
                    dict(shared, event="native_combat_diagnostics", duration_qpc=10,
                         flags=0, values=[0] * 16)]
        combat_header = dict(event="header", qpc=1, schema=2, qpc_hz=1000,
                             capture_domain="combat", combat_hook=1,
                             combat_detail_schema=1)
        combat_footer = dict(event="footer", qpc=500, dropped=0, interrupted=False,
                             io_error=False)
        tracking_only = dict(event="tracking", qpc=10, seq=7, epoch=1, predicted_ns=900)
        def combat_capture(records, context_schema=None):
            header = combat_header if context_schema is None else dict(
                combat_header, combat_context_schema=context_schema)
            return capture_report([header, tracking_only] + records +
                                  [dict(combat_footer, events=len(records) + 1)])
        samples = (combat_case_sample(150, 1, -25) + combat_case_sample(200, 2, 0) +
                   combat_case_sample(250, 3, -30) + combat_case_sample(400, 4, -10))
        # The tracked-blade stream the bench emits on the same clock. Flag bit 0 is
        # our own geometry classification, never a native outcome.
        def combat_bench_sample(qpc, seq, flags, tip_left, tip_right,
                                world_scale=0.9):
            return dict(event="combat_bench_provisional", qpc=qpc, seq=seq, epoch=1,
                        flags=flags,
                        values=[tip_left, tip_right] + [0.0] * 11 +
                               [world_scale, 0.0, 0.0])
        def combat_motion_sample(qpc, seq, flags, values, native_rate=0x12345678):
            return dict(event="native_combat_motion", qpc=qpc, seq=seq, thread=7,
                        object="1403c4020", flags=flags, native_rate=native_rate,
                        values=values)
        motion_ok = combat_motion_sample(150, 1, 31, [0, 0, -1, 0, 0, 0, -4.5, 0,
                                            1.0, 4.5] + [0.0] * 6)
        # Two of the four callbacks carry the native record's own direction and
        # velocity: one full read with a unit direction, one direction-only read.
        # The other two carry none, which is what an older build's capture looks
        # like and must stay valid rather than become an error.
        bench_near = combat_bench_sample(148, 1, 1, 3.0, 0.0)
        records = sorted(samples + [bench_near,
                                    combat_bench_sample(205, 2, 0, 0.5, 0.0),
                                    combat_bench_sample(398, 4, 1, 4.0, 0.0),
                                    motion_ok,
                                    combat_motion_sample(
                                        250, 3, 5, [0.7, 0, 0, 0] + [0.0] * 4 +
                                        [0.7, 0] + [0.0] * 6)],
                         key=lambda event: event["qpc"])
        cases_path = Path(directory) / "combat-cases.json"
        def case_report(cases):
            cases_path.write_text(json.dumps(cases), encoding="utf-8")
            return combat_capture(records)
        report = case_report(dict(schema=1, qpc_hz=1000, cases=[
            dict(label="hit", start_qpc=100, end_qpc=200),
            dict(label="miss", start_qpc=201, end_qpc=300)]))
        plan, cases = report["combat_case_plan"], report["native_combat_observations"]
        check(cases["paired_samples"] == 4 and plan["labels"] == ["hit", "miss"] and
              not plan["errors"],
              "declared case windows rejected or samples lost")
        check(cases["case_observations"]["hit"]["samples"] == 2 and
              cases["case_observations"]["hit"]["hp_delta_values"] == [-25, 0] and
              cases["case_observations"]["hit"]["samples_with_hp_loss"] == 1 and
              cases["case_observations"]["hit"]["samples_with_hp_change"] == 1 and
              cases["case_observations"]["miss"]["samples"] == 1 and
              cases["case_observations"]["miss"]["hp_delta_values"] == [-30] and
              cases["case_observations"]["miss"]["samples_with_hp_change"] == 1,
              "case windows mis-bucketed or an HP change invented from a label")
        check(len(cases["case_observations"]) == 2 and
              sum(sample["case"] is None for sample in cases["samples"]) == 1,
              "an out-of-window sample was invented into a declared case")
        check(cases["diagnostic_samples"] == 4 and
                cases["read_reason_counts"] == {"valid": 16} and
              all(sample["target"]["handle"] == 0xabcd and
                  sample["source"]["handle"] == 0x12345678
                  for sample in cases["samples"] if sample["case"] == "hit"),
              "case binding lost actor identity or refusal reasons")
        # Per-case reasons matter: a window where every actor read was refused looks
        # identical to a window with no samples unless the reasons are attributed.
        check(cases["case_observations"]["hit"]["read_reason_counts"] == {"valid": 8} and
              cases["case_observations"]["hit"]["diagnostic_samples"] == 2 and
              cases["case_observations"]["miss"]["read_reason_counts"] == {"valid": 4} and
              cases["case_observations"]["miss"]["diagnostic_samples"] == 1 and
              all(sample["read_reasons"] == ["valid"] * 4 for sample in cases["samples"]
                  if sample["case"] is not None),
              "per-case read reasons missing, mis-attributed or invented")
        check(any("Case labels bind by QPC window only" in text
                  for text in cases["coverage_limitations"]),
              "declared labels outranked observed evidence")
        # Coverage is separate from binding: a sample can sit inside a window that
        # this capture only recorded part of, and a window can sit entirely outside
        # the capture's own header/footer span.
        check(plan["coverage_status"] == "PASS" and not plan["uncovered_labels"] and
              not plan["unobserved_labels"] and not plan["truncated_labels"] and
              plan["coverage"]["hit"]["capture_overlap_ms"] == 100.0 and
              plan["coverage"]["hit"]["capture_covers_window"] and
              cases["case_observations"]["hit"]["capture_covers_window"],
              "capture coverage of the declared windows not reported")
        # A window cut short by the deadline or a stop request is stamped exactly
        # like a completed one, so only the plan's own declared duration separates
        # "the operator had the full time" from "the transaction ended first".
        short = case_report(dict(schema=1, qpc_hz=1000, cases=[
            dict(label="hit", start_qpc=100, end_qpc=200, seconds=45, state="declared")]))
        check(short["combat_case_plan"]["truncated_labels"] == ["hit"] and
              short["combat_case_plan"]["coverage"]["hit"]["measured_seconds"] == 0.1 and
              short["combat_case_plan"]["coverage_status"] == "PASS" and
              any("cut short before their declared duration" in text
                  for text in short["native_combat_observations"]["coverage_limitations"]),
              "a window cut short stayed indistinguishable from a full one")
        # A window outside this capture's span has no observation at all. Its zero
        # sample count must read as a missing capture, never as the declared action
        # producing no native contact.
        far = case_report(dict(schema=1, qpc_hz=1000, cases=[
            dict(label="late", start_qpc=600, end_qpc=700, seconds=100, state="declared")]))
        far_plan, far_cases = far["combat_case_plan"], far["native_combat_observations"]
        check(far_plan["coverage_status"] == "NONE" and far["valid"] and
              far_plan["unobserved_labels"] == ["late"] and
              far_plan["coverage"]["late"]["capture_overlap_ms"] == 0.0 and
              far_cases["case_observations"]["late"]["samples"] == 0 and
              far["checks"]["combat_case_coverage"]["status"] == "NONE" and
              any("no capture overlap at all" in text
                  for text in far_cases["coverage_limitations"]),
              "a window this capture never covered read as a negative result")
        # A window can straddle the capture edge: part of it is recorded, the rest
        # never was. Those gaps must not collapse into one label, or a partly covered
        # window's floor reads like a never-recorded window's nothing.
        mixed = case_report(dict(schema=1, qpc_hz=1000, cases=[
            dict(label="edge", start_qpc=400, end_qpc=700),
            dict(label="late", start_qpc=750, end_qpc=850)]))
        mixed_plan = mixed["combat_case_plan"]
        mixed_cases = mixed["native_combat_observations"]
        check(mixed_plan["partly_covered_labels"] == ["edge"] and
              mixed_plan["unobserved_labels"] == ["late"] and
              mixed_plan["coverage_status"] == "PARTIAL" and
              mixed_plan["coverage"]["edge"]["capture_overlap_ms"] == 100.0 and
              not mixed_plan["truncated_labels"] and
              any("overlaps but does not cover end to end: edge" in text
                 for text in mixed_cases["coverage_limitations"]) and
              not any("no capture overlap at all: edge" in text
                     for text in mixed_cases["coverage_limitations"]),
              "a straddled window and a never-recorded window reported the same gap")
        # A capture whose footer never landed has no span, so its coverage is unknown.
        # Reporting NONE there would tell the next reader the operator never performed
        # the case - the same false negative, one layer down.
        unclosed = capture_report([combat_header, tracking_only] + records)
        unclosed_plan = unclosed["combat_case_plan"]
        unclosed_cases = unclosed["native_combat_observations"]
        check(unclosed_plan["coverage_status"] == "UNVERIFIED" and
              unclosed["checks"]["combat_case_coverage"]["status"] == "UNVERIFIED" and
              not unclosed_plan["unobserved_labels"] and
              not unclosed_plan["partly_covered_labels"] and
              not unclosed_plan["uncovered_labels"] and
              unclosed_plan["coverage"]["edge"]["capture_overlap_ms"] is None and
              unclosed_plan["coverage"]["edge"]["capture_covers_window"] is None and
              unclosed_cases["case_observations"]["edge"]["capture_covers_window"] is None and
              any("never wrote its closing footer" in text
                  for text in unclosed_cases["coverage_limitations"]),
              "an unclosed capture reported case coverage as a negative result")
        case_report(dict(schema=1, qpc_hz=1000, cases=[
            dict(label="hit", start_qpc=100, end_qpc=200),
            dict(label="miss", start_qpc=201, end_qpc=300)]))
        # Attribution joins the two streams per case. It must keep them distinct:
        # a tracked classification near a callback is proximity, and an HP loss
        # with no tracked classification nearby must not read as our blade.
        attribution = cases["case_attribution"]
        check(cases["case_observations"].keys() == attribution.keys() and
              attribution["hit"]["bench_samples"] == 1 and
              attribution["hit"]["bench_classification_frames"]["geometry_hit"] == 1 and
              attribution["hit"]["bench_max_tip_speed_mps"] == 3.0 and
              attribution["hit"]["contacts"] == 2 and
              attribution["hit"]["contacts_with_hp_loss"] == 1 and
              attribution["hit"]["hp_loss_without_near_tracked_hit"] == 0 and
              attribution["miss"]["bench_samples"] == 1 and
              attribution["miss"]["bench_classification_frames"]["geometry_hit"] == 0 and
              attribution["miss"]["hp_loss_without_near_tracked_hit"] == 1,
              "tracked-blade/native-contact attribution mis-bucketed or promoted to proof")
        near_hit, far_hit = attribution["hit"]["contact_proximity"]
        check(near_hit["bench_qpc"] == 148 and near_hit["lag_ms"] == 2.0 and
              near_hit["bench_tip_speed_mps"] == 3.0 and
              near_hit["bench_geometry_hit"] is True and
              far_hit["bench_qpc"] == 205 and far_hit["lag_ms"] == -5.0 and
              far_hit["bench_geometry_hit"] is False,
              "contact proximity reached past the near window or hid the measured lag")
        # Motion joins by (thread, seq, object) and is cross-checked against the
        # callback's own native return. Absence stays valid; a record bound to a
        # different return, or a second record for one callback, is a disagreement
        # that invalidates the capture rather than a hint to be ignored.
        check(cases["motion_samples"] == 2 and
              cases["case_observations"]["hit"]["native_motion_samples"] == 1 and
              cases["case_observations"]["hit"]["native_unit_direction_samples"] == 1 and
              cases["case_observations"]["hit"]["native_speed_units_per_s_values"] == [4.5] and
              cases["case_observations"]["miss"]["native_motion_samples"] == 1 and
              cases["case_observations"]["miss"]["native_unit_direction_samples"] == 0 and
              cases["case_observations"]["miss"]["native_speed_units_per_s_values"] == [],
              "native motion counts, unit-length bits or speeds mis-attributed")
        by_seq = {sample["seq"]: sample for sample in cases["samples"]}
        # The selected native row is separate from the animation/behavior ID.
        # Full signed dwords survive float32 transport through 16-bit limbs;
        # type 64 is a context observation, never a successful-parry verdict.
        context = dict(event="native_combat_context", qpc=150, seq=1, thread=7,
                       object="1403c4020", flags=7, native_rate=0x12345678,
                       values=[64,0,0x5678,0x1234,6000,0,1,0,65534,65535,
                               41248,7,19,1,47768,65244])
        response_values = [15, 2, 0, 47768, 65244, 165, 90, 1, 0] + [0]*6 + [1]
        response_record = dict(samples[2], values=response_values)
        response_report = combat_capture(samples[:2] + [response_record])
        check(response_report["valid"], "raw native response fields rejected")
        response = response_report["native_combat_observations"]["samples"][0]["native_response"]
        check(response == dict(schema=1, kind_before=2, kind_after=0xfedcba98,
                               status_before=[165,90], status_after=[1,0]),
              "native response kind/status lost or truncated")
        check(response_report["native_combat_observations"]["status"] == "UNVERIFIED",
              "raw response fields promoted to a defense verdict")
        check(combat_capture(samples[:3])["native_combat_observations"]["samples"][0]["native_response"] is None,
              "legacy capture invented native response fields")
        for mask in range(16):
            values = response_values.copy()
            values[0] = mask
            for bit, start, end in ((1,1,3), (2,3,5), (4,5,7), (8,7,9)):
                if not mask & bit:
                    values[start:end] = [0]*(end-start)
            partial = combat_capture(samples[:2] + [dict(response_record, values=values)])
            check(partial["valid"], "partial native response read rejected")
            decoded_response = partial["native_combat_observations"]["samples"][0]["native_response"]
            check((decoded_response["kind_before"] is not None) == bool(mask & 1) and
                  (decoded_response["kind_after"] is not None) == bool(mask & 2) and
                  (decoded_response["status_before"] is not None) == bool(mask & 4) and
                  (decoded_response["status_after"] is not None) == bool(mask & 8),
                  "unread native response manufactured as zero")
        for index, value in ((0,16), (0,32), (1,65536), (3,-1), (5,256), (7,.5), (9,1),
                               (10,65536), (14,256), (15,0), (15,3)):
            bad_values = response_values.copy()
            bad_values[index] = value
            check(not combat_capture(samples[:2] + [dict(response_record, values=bad_values)])["valid"],
                  "malformed native response accepted")
        # Version 2 carries the 140529670 store group; v1 payloads must keep
        # slots 9-14 zero, v2 must carry a well-formed store group.
        store_values = response_values.copy()
        store_values[15] = 2
        store_values[0] = 31
        store_values[9] = 2.5
        store_values[10:14] = [0x3344, 0x1122, 0x7788, 0x5566]
        store_values[14] = 0x7e
        check(combat_capture(samples[:2] + [dict(response_record, values=store_values)])["valid"],
              "well-formed v2 store group rejected")
        decoded_store = combat_capture(samples[:2] + [dict(response_record, values=store_values)]
                                       )["native_combat_observations"]["samples"][0]["native_response"]
        check(decoded_store["schema"] == 2 and decoded_store["store_weight_after"] == 2.5 and
              decoded_store["store_paired_ac"] == 0x11223344 and
              decoded_store["store_paired_b0"] == 0x55667788 and
              decoded_store["store_tag_b4"] == 0x7e,
              "v2 store group lost or truncated")
        bad_store = store_values.copy()
        bad_store[0] = 15  # mask drops the store bit: nonzero store fields refused
        check(not combat_capture(samples[:2] + [dict(response_record, values=bad_store)])["valid"],
              "v2 store group accepted without read-validity bit")
        masked_values = response_values.copy()
        masked_values[0] = 0
        check(not combat_capture(samples[:2] + [dict(response_record, values=masked_values)])["valid"],
              "native response payload accepted without read-validity bits")
        contextual = combat_capture(samples[:3] + [context], context_schema=1)
        native = contextual["native_combat_observations"]
        decoded = native["samples"][0]["native_context"]
        check(contextual["valid"] and native["context_samples"] == 1 and
              decoded["behavior_id"] == 0x12345678 and decoded["attack_param_id"] == 6000 and
              decoded["attack_type"] == 64 and decoded["table_selector"] == 1 and
              decoded["geometry_selector"] == -2 and decoded["weapon_id"] == 500000 and
              decoded["payload_flags_c8"] == 19 and decoded["payload_flags_c9"] == 1 and
              decoded["event_handle"] == 0xfedcba98 and
              native["case_observations"]["hit"]["attack_selections"][0]["attack_param_id"] == 6000 and
              native["status"] == "UNVERIFIED",
              "native context IDs/flags lost or promoted to a defense verdict")
        for changed in (dict(seq=2), dict(thread=8), dict(object="1403c4021"),
                        dict(native_rate=0x87654321)):
            bad = combat_capture(samples[:3] + [dict(context, **changed)], context_schema=1)
            check(not bad["valid"], "mis-bound native context accepted")
        check(not combat_capture(samples[:3], context_schema=1)["valid"],
              "declared context schema allowed a missing context")
        check(not combat_capture(samples[:3]+[context,context], context_schema=1)["valid"],
              "duplicate native context accepted")
        check(combat_capture(samples[:3])["valid"], "older context-free capture rejected")
        partial_context = dict(context, flags=2, values=[0]*12+[19,1,0,0])
        partial_report = combat_capture(samples[:3]+[partial_context], context_schema=1)
        check(partial_report["valid"] and
              partial_report["native_combat_observations"]["samples"][0]["native_context"]["attack_param_id"] is None,
              "unreadable native selection manufactured as row zero")
        for changed in (dict(flags=8), dict(flags=True), dict(native_rate=1<<32),
                        dict(values=[0]*14+[65536,0]), dict(flags=0),
                        dict(values=[-1]+context["values"][1:]),
                        dict(values=context["values"][:12]+[256,1,0,0])):
            check(not combat_capture(samples[:3]+[dict(context, **changed)])["valid"],
                  "malformed native context accepted")
        check(by_seq[2]["native_motion"] is None and
              by_seq[1]["native_motion"]["direction"] == [0, 0, -1] and
              by_seq[1]["native_motion"]["direction_length"] == 1.0 and
              by_seq[1]["native_motion"]["speed_units_per_s"] == 4.5 and
              by_seq[3]["native_motion"]["direction"] == [0.7, 0, 0] and
              by_seq[3]["native_motion"]["velocity"] is None and
              by_seq[3]["native_motion"]["speed_units_per_s"] is None,
              "native motion fields invented, truncated or gated by the wrong bit")
        check(attribution["hit"]["native_motion_samples"] == 1 and
              attribution["hit"]["native_speed_units_per_s_values"] == [4.5] and
              near_hit["world_scale"] == 0.9 and
              near_hit["native_speed_units_per_s"] == 4.5 and
              near_hit["native_speed_mps"] == 5.0 and
              attribution["hit"]["native_speed_mps_values"] == [5.0] and
              attribution["hit"]["bench_minus_native_speed_mps"] == [-2.0] and
              attribution["hit"]["native_speed_without_world_scale"] == 0 and
              near_hit["native_direction_unit"] is True and
              far_hit["native_speed_units_per_s"] is None and
              far_hit["native_speed_mps"] is None,
              "measured and native swing speed never compared in one unit for the same swing")
        disagree = combat_capture(samples[:3] + [combat_motion_sample(
                                            150, 1, 31, motion_ok["values"], 0x87654321)])
        check(not disagree["valid"] and disagree["native_combat_observations"]["motion_samples"] == 0 and
              any("contact motion payload disagrees" in t for t in disagree["errors"]),
              "motion bound to another native return accepted as extra evidence")
        doubled = combat_capture(samples[:3] + [motion_ok, dict(motion_ok, qpc=151)])
        check(not doubled["valid"] and
              any("contact motion pairing" in t for t in doubled["errors"]),
              "two motion records for one callback silently averaged or dropped")
        orphan = combat_capture([motion_ok])
        check(not orphan["valid"] and
              any("contact identity pairing" in t for t in orphan["errors"]),
              "motion record with no contact behind it accepted")
        # A native speed with no applied world scale stays a raw unit count: the
        # analyzer must not silently call it metres per second.
        unsealed = combat_capture(samples[:3] + [motion_ok, dict(
            bench_near, values=bench_near["values"][:13] + [0.0, 0.0, 0.0])])
        unsealed_attribution = unsealed["native_combat_observations"]["case_attribution"]
        check(unsealed["valid"] and
              unsealed_attribution["hit"]["native_speed_without_world_scale"] == 1 and
              unsealed_attribution["hit"]["native_speed_mps_values"] == [] and
              unsealed_attribution["hit"]["native_speed_units_per_s_values"] == [4.5] and
              unsealed_attribution["hit"]["bench_minus_native_speed_mps"] == [],
              "a raw native unit speed reported as metres per second with no world scale")
        check(any("case_attribution is temporal proximity" in text
                for text in cases["coverage_limitations"]),
              "attribution reported without its correlation-only limit")
        for cases, reason in ((dict(schema=1, qpc_hz=10000000, cases=[
                                  dict(label="hit", start_qpc=100, end_qpc=200)]),
                              "case plan clock differs from capture clock"),
                             (dict(schema=1, qpc_hz=1000, cases=[
                                  dict(label="a", start_qpc=100, end_qpc=300),
                                  dict(label="b", start_qpc=250, end_qpc=400)]),
                              "case plan windows overlap"),
                             (dict(schema=1, qpc_hz=1000, cases=[
                                  dict(label="a", start_qpc=300, end_qpc=100)]),
                              "case plan declares a malformed window"),
                             (dict(schema=1, qpc_hz=1000, cases=[
                                  dict(label="a", start_qpc=100, end_qpc=200),
                                  dict(label="a", start_qpc=201, end_qpc=300)]),
                              "case plan repeats label a"),
                             (dict(schema=2, qpc_hz=1000, cases=[]),
                              "case plan has no supported schema")):
            bad = case_report(cases)
            bad_plan = bad["combat_case_plan"]
            check(reason in bad_plan["errors"] and bad_plan["windows"] == 0 and
                  bad_plan["status"] == "NOT_APPLICABLE" and
                  bad["native_combat_observations"]["case_observations"] == {} and
                  bad["checks"]["combat_observation_integrity"]["status"] == "FAIL" and
                  not bad["valid"] and bad["native_combat_observations"]["paired_samples"] == 4,
                  "unusable case plan accepted or samples lost")
        cases_path.unlink()
        absent = combat_capture(records)
        check(absent["combat_case_plan"]["status"] == "NOT_APPLICABLE" and
              absent["combat_case_plan"]["windows"] == 0 and not absent["combat_case_plan"]["errors"] and
              absent["native_combat_observations"]["case_observations"] == {} and
              absent["checks"]["combat_observation_integrity"]["status"] == "PASS" and
              absent["valid"],
              "missing case plan invented windows or failed an otherwise clean capture")
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
        if len(sys.argv) > 2:
            path.unlink()
            subprocess.run([sys.argv[2], "--fixture", str(path)], check=True,
                           timeout=10, capture_output=True)
            combat = read_capture(path)
            observations = combat["native_combat_observations"]
            check(combat["checks"]["capture_integrity"]["status"] == "PASS",
                  "actual contact writer/reader round-trip failed")
            check(observations["status"] == "UNVERIFIED" and not combat["complete"],
                  "synthetic HP delta certified physical combat")
            check(observations["paired_samples"] == 1 and not observations["pairing_errors"],
                  "contact and identity records not paired")
            check(observations["diagnostic_samples"] == 1 and
                  observations["read_reason_counts"] == {"valid": 4} and
                  observations["timings"]["observer_read"]["count"] == 1,
                  "observer cost or explicit read reasons missing")
            sample = observations["samples"][0]
            check(observations["context_samples"] == 1 and
                  sample["native_context"]["behavior_id"] == 0x12345678 and
                  sample["native_context"]["attack_param_id"] == 6000 and
                  sample["native_context"]["attack_type"] == 64 and
                  sample["native_context"]["geometry_selector"] == -2 and
                  sample["native_context"]["event_handle"] == 0xfedcba98,
                  "actual detour/writer/analyzer lost the pre-dispatch context")
            check(sample["source"]["handle"] == 0x12345678 and
                  sample["target"]["handle"] == 0xfedcba98 and
                  sample["native_return"] == "12345678abcdef01",
                  "actor lifetime or native return lost precision")
            check(sample["source"]["hp_delta"] == 0 and
                  sample["target"]["hp_delta"] == -25 and
                  sample["target"]["stamina_delta"] == -20,
                  "actual before/after observations decoded incorrectly")
            raw = [json.loads(line) for line in path.read_text().splitlines()]
            identity_record = next(e for e in raw if e["event"] == "native_combat_identity")
            contact_record = next(e for e in raw if e["event"] == "native_combat_contact")
            diagnostic_record = next(e for e in raw if e["event"] == "native_combat_diagnostics")
            def combat_report(records):
                footer = dict(raw[-1], events=len(records))
                return capture_report([raw[0]] + records + [footer])
            legacy = combat_report([contact_record])["native_combat_observations"]
            check(legacy["paired_samples"] == 0 and legacy["unpaired_contacts"] == 1,
                  "legacy record invented an actor lifetime")
            duplicate = combat_report([contact_record, identity_record, identity_record])
            check(duplicate["native_combat_observations"]["paired_samples"] == 0 and
                  duplicate["native_combat_observations"]["pairing_errors"],
                  "duplicate identity silently selected")
            mismatched = dict(identity_record, thread=identity_record["thread"] + 1)
            mismatch = combat_report([contact_record, mismatched])["native_combat_observations"]
            check(mismatch["paired_samples"] == 0 and mismatch["pairing_errors"],
                  "callbacks on different threads conflated")
            changed = combat_report([dict(contact_record, flags=7),
                                     dict(identity_record, flags=7)])
            target = changed["native_combat_observations"]["samples"][0]["target"]
            check(target["hp_delta"] is None and target["stamina_delta"] is None,
                  "replaced target retained its old outcome")
            for fields in (dict(flags=True), dict(flags=4), dict(seq=0), dict(thread=0),
                           dict(object="not-an-address"), dict(values=[.5] + [0] * 15),
                           dict(values=[65536] + [0] * 15), dict(values=[True] + [0] * 15)):
                bad = combat_report([contact_record, dict(identity_record, **fields)])
                check(bad["checks"]["capture_integrity"]["status"] == "FAIL",
                      "malformed combat identity accepted")
            full_sample = [contact_record, identity_record, diagnostic_record]
            for flags in (True, -1, 65536, 10, 0xf000):
                bad = combat_report(full_sample[:2] + [dict(diagnostic_record, flags=flags)])
                check(bad["checks"]["capture_integrity"]["status"] == "FAIL",
                      "unknown refusal code accepted")
            bad = combat_report(full_sample[:2] + [dict(diagnostic_record, flags=8)])
            check(bad["checks"]["combat_observation_integrity"]["status"] == "FAIL",
                  "read-validity/diagnostic disagreement accepted")
            duplicate = combat_report(full_sample + [diagnostic_record])
            check(duplicate["checks"]["combat_observation_integrity"]["status"] == "FAIL",
                  "duplicate refusal diagnostics silently selected")
            missing = combat_report(full_sample[:2])
            check(missing["checks"]["combat_observation_integrity"]["status"] == "FAIL",
                  "new-schema sample silently accepted without diagnostics")
            # Small samples must not hide the only expensive call in p95/p99.
            second = [dict(e, seq=e["seq"] + 1, duration_qpc=100) for e in full_sample]
            timing = combat_report(full_sample + second)["native_combat_observations"]["timings"]
            expected_max = max(contact_record["duration_qpc"], 100) * 1000 / raw[0]["qpc_hz"]
            check(timing["native_call"]["p99_ms"] == expected_max and
                  timing["native_call"]["p95_ms"] == expected_max,
                  "tail timing underestimated on small samples")
    interaction = dict(event="native_interaction_activation", qpc=1, thread=7, seq=1,
                       object="10000", flags=15,
                       values=[0, 0, 65535, 65535, 22136, 4660, 0, 0, 3, 0,
                               61185, 43981, 22136, 4660, 90, 0])
    header = dict(event="header", qpc=0, schema=2, capture_domain="combat", qpc_hz=1000)
    footer = dict(event="footer", qpc=2, dropped=0, events=1, interrupted=False,
                  io_error=False, combat_total=2, combat_non_player=0, combat_self=2,
                  combat_refused=0, combat_detail=0, combat_detail_limit=4294967295)
    import tempfile
    with tempfile.TemporaryDirectory() as temporary:
        observation_path = Path(temporary) / "interaction.jsonl"
        def interaction_report(rows):
            observation_path.write_text("\n".join(json.dumps(row) for row in rows) + "\n")
            return read_capture(observation_path)
        observed = interaction_report([header, interaction, footer])
        check(observed["valid"] and observed["native_interaction_observations"]["activation_callbacks"] == 1,
              "native object-action observation rejected")
        check(not observed["complete"] and observed["native_interaction_observations"]["status"] == "UNVERIFIED",
              "object-action request certified physical interaction")
        for bad in (dict(interaction, flags=True), dict(interaction, flags=4),
                    dict(interaction, flags=8), dict(interaction, seq=0),
                    dict(interaction, values=[0.5] + interaction["values"][1:])):
            check(not interaction_report([header, bad, footer])["valid"], "malformed interaction evidence accepted")
        for fields in (dict(combat_self=True), dict(combat_self=-1), dict(combat_total=3),
                       dict(combat_detail=1, combat_detail_limit=0)):
            check(not interaction_report([header, interaction, dict(footer, **fields)])["valid"],
                  "malformed continuous callback counts accepted")
    print("test_vr_trace: checks passed")


if __name__ == "__main__":
    main()
