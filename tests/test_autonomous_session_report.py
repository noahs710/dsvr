"""Public CLI regression for indexed autonomous-session telemetry."""
import json
import subprocess
import sys
import tempfile
from pathlib import Path


def write_jsonl(path, records):
    path.write_text("".join(json.dumps(row, separators=(",", ":")) + "\n"
                              for row in records), encoding="utf-8")


def fixture(root, name, *, gaps=(), dropped=False, incomplete=False, contended=False):
    session_id = "session-test-42"
    prefix = root / name
    index_path = Path(str(prefix) + ".jsonl")
    records = [dict(event="session_start", schema=1, session_id=session_id,
                    pid=123, qpc_hz=1000, qpc=10, utc_filetime_100ns=1)]
    if incomplete:
        records.extend([
            dict(event="epoch_start", session_id=session_id, epoch=1, qpc=20,
                 utc_filetime_100ns=1),
            dict(event="segment_start", session_id=session_id, epoch=1, segment=1,
                 qpc=100, gap_qpc=0, segment_file="epoch-1-segment-1.jsonl",
                 capabilities=255, config_flags=3, state="starting"),
        ])
        footer = None
        segment_count = 1
    else:
        records.append(dict(event="epoch_start", session_id=session_id, epoch=1,
                            qpc=20, utc_filetime_100ns=1))
        segment_count = len(gaps) + 1
        footer = None

    for segment in range(1, segment_count + 1):
        gap = 0 if segment == 1 else gaps[segment - 2]
        leaf = f"epoch-1-segment-{segment}.jsonl"
        segment_path = Path(str(prefix) + "-" + leaf)
        records.append(dict(event="segment_start", session_id=session_id, epoch=1,
                            segment=segment, qpc=100 + segment * 100,
                            gap_qpc=gap, segment_file=leaf, capabilities=255,
                            config_flags=3, state="starting"))
        header = dict(event="header", qpc=100 + segment * 100, schema=2, pid=123,
                      qpc_hz=1000, duration_ms=600000, mod_version="fixture",
                      combat_detail_schema=0, combat_context_schema=0, combat_hook=0,
                      capture_domain="session", session_id=session_id,
                      player_epoch=1, segment_index=segment,
                      utc_filetime_100ns=1, session_schema=1,
                      session_capabilities=255, session_config_flags=3,
                      hand_sample_hz_max=30)
        if incomplete:
            write_jsonl(segment_path, [header])
            break
        pad = dict(event="virtual_pad_state", qpc=101 + segment * 100,
                   predicted_ns=1100000 + segment * 1000000, epoch=4, flags=1,
                   values=[0, 0, 0, 100, 0, 0, 0] + [0] * 9)
        hand_valid = dict(event="hand_tracking", qpc=102 + segment * 100,
                          predicted_ns=1100000 + segment * 1000000, epoch=4,
                          flags=15, values=[0, 0, 1, 2, 0, 0, 0, 1,
                                            1, 2, 3, .1, .2, .3, 5, 0])
        hand_invalid = dict(event="hand_tracking", qpc=103 + segment * 100,
                            predicted_ns=1100000 + segment * 1000000, epoch=4,
                            flags=2, values=[1] + [0] * 13 + [0, 0])
        footer = dict(event="footer", qpc=200 + segment * 100,
                      dropped=1 if dropped or contended else 0, events=3,
                      interrupted=False, io_error=False, queue_peak=0, contended=1 if contended else 0,
                      combat_total=0, combat_non_player=0, combat_self=0,
                      combat_refused=0, combat_detail=0, combat_detail_limit=0)
        write_jsonl(segment_path, [header, pad, hand_valid, hand_invalid, footer])
        records.append(dict(event="segment_end", session_id=session_id, epoch=1,
                            segment=segment, qpc=footer["qpc"], duration_qpc=100,
                            reason="dropped_events" if dropped or contended else "rotation",
                            state="dropped" if dropped or contended else "complete",
                            capture_id=segment, events=3,
                            dropped=1 if dropped or contended else 0, contended=1 if contended else 0,
                            queue_peak=0, io_error=False))

    if not incomplete:
        records.append(dict(event="epoch_end", session_id=session_id, epoch=1,
                            qpc=500, reason="player_unload", degraded=dropped or contended))
        records.append(dict(event="session_end", session_id=session_id, qpc=600,
                            reason="process_shutdown"))
    write_jsonl(index_path, records)
    return index_path


def analyze(cli, path):
    result = subprocess.run([sys.executable, str(cli), str(path)],
                            text=True, capture_output=True, timeout=15)
    return result, json.loads(result.stdout)


def mutate_jsonl(path, change):
    rows = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
    change(rows)
    write_jsonl(path, rows)


def reappearance_fixture(root):
    index = fixture(root, "reappearance")
    rows = [json.loads(line) for line in index.read_text(encoding="utf-8").splitlines()]
    session_id = rows[0]["session_id"]
    trace = Path(str(index)[:-6] + "-epoch-1-segment-1.jsonl")
    trace_rows = [json.loads(line) for line in trace.read_text(encoding="utf-8").splitlines()]
    for row in trace_rows:
        row["qpc"] += 1000000
    trace_rows[0]["player_epoch"] = 2
    leaf = "epoch-2-segment-1.jsonl"
    write_jsonl(Path(str(index)[:-6] + "-" + leaf), trace_rows)
    rows.pop()
    rows.extend([
        dict(event="epoch_start", session_id=session_id, epoch=2, qpc=1000100),
        dict(event="segment_start", session_id=session_id, epoch=2, segment=1,
             qpc=trace_rows[0]["qpc"], gap_qpc=999900, segment_file=leaf),
        dict(event="segment_end", session_id=session_id, epoch=2, segment=1,
             qpc=trace_rows[-1]["qpc"], dropped=0, contended=0, io_error=False),
        dict(event="epoch_end", session_id=session_id, epoch=2,
             qpc=1000400, reason="player_unload"),
        dict(event="session_end", session_id=session_id, qpc=1000500),
    ])
    write_jsonl(index, rows)
    return index


def main():
    cli = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="dsvr-session-report-") as temporary:
        root = Path(temporary)
        complete_path = fixture(root, "complete")
        result, complete = analyze(cli, complete_path)
        assert result.returncode == 0, result.stderr + result.stdout
        assert complete["report_type"] == "autonomous_vr_session"
        assert complete["valid"] and complete["complete"]
        assert complete["acceptance"] == "UNVERIFIED"
        assert complete["unknowns"]["scene_identity"] == "unknown"
        assert complete["input_observations"]["virtual_pad_state_changes"] == 1
        assert complete["input_observations"]["valid_hand_samples"] == 1
        assert complete["input_observations"]["hand_validity_or_space_transitions"] == 2

        result, reappeared = analyze(cli, reappearance_fixture(root))
        assert result.returncode == 0 and reappeared["complete"], result.stderr + result.stdout
        assert reappeared["coverage"]["continuous"] and not reappeared["coverage"]["gaps"]

        for field, wrong in (("pid", 999), ("qpc_hz", 2000), ("capture_domain", "full")):
            bad_index = fixture(root, "wrong-" + field)
            trace = Path(str(bad_index)[:-6] + "-epoch-1-segment-1.jsonl")
            mutate_jsonl(trace, lambda rows: rows[0].update({field: wrong}))
            result, report = analyze(cli, bad_index)
            assert result.returncode == 2 and not report["complete"], (field, report)

        hidden_gap = fixture(root, "hidden-gap", gaps=(0,))
        trace = Path(str(hidden_gap)[:-6] + "-epoch-1-segment-2.jsonl")
        mutate_jsonl(trace, lambda rows: rows[0].update(qpc=342))
        result, report = analyze(cli, hidden_gap)
        assert result.returncode == 2 and not report["coverage"]["continuous"], report
        assert report["coverage"]["gaps"][0]["gap_qpc"] == 42

        orphan = fixture(root, "orphan")
        mutate_jsonl(orphan, lambda rows: rows.__setitem__(slice(None),
            [row for row in rows if row["event"] not in ("epoch_start", "epoch_end")]))
        result, report = analyze(cli, orphan)
        assert result.returncode == 2 and not report["complete"], report

        for boundary in ("neither", "start-only", "end-only"):
            orphan_epoch = fixture(root, "orphan-epoch-" + boundary)
            def orphan_epoch_records(rows):
                for row in rows:
                    if row["event"] in ("segment_start", "segment_end"):
                        row["epoch"] = 2
                if boundary == "start-only":
                    rows.insert(-1, dict(event="epoch_start", session_id=rows[0]["session_id"],
                                         epoch=2, qpc=190))
                elif boundary == "end-only":
                    rows.insert(-1, dict(event="epoch_end", session_id=rows[0]["session_id"],
                                         epoch=2, qpc=400, reason="player_unload"))
            mutate_jsonl(orphan_epoch, orphan_epoch_records)
            trace = Path(str(orphan_epoch)[:-6] + "-epoch-1-segment-1.jsonl")
            mutate_jsonl(trace, lambda rows: rows[0].update(player_epoch=2))
            result, report = analyze(cli, orphan_epoch)
            assert result.returncode == 2 and not report["complete"], (boundary, report)
            assert any("epoch 2" in error for error in report["errors"]), report

        empty_epoch = fixture(root, "empty-epoch")
        def empty_epoch_records(rows):
            rows.insert(-1, dict(event="epoch_start", session_id=rows[0]["session_id"],
                                 epoch=3, qpc=300))
            rows.insert(-1, dict(event="epoch_end", session_id=rows[0]["session_id"],
                                 epoch=3, qpc=380, reason="player_unload", degraded=False))
        mutate_jsonl(empty_epoch, empty_epoch_records)
        result, report = analyze(cli, empty_epoch)
        assert result.returncode == 2 and not report["complete"], report
        assert any("epoch 3 has no captured segments" in error for error in report["errors"]), report

        outside_epoch = fixture(root, "outside-epoch")
        mutate_jsonl(outside_epoch, lambda rows: next(
            row for row in rows if row["event"] == "epoch_start").update(qpc=250))
        result, report = analyze(cli, outside_epoch)
        assert result.returncode == 2 and not report["complete"], report

        session_end_regression = fixture(root, "session-end-regression")
        mutate_jsonl(session_end_regression, lambda rows: next(
            row for row in rows if row["event"] == "session_end").update(qpc=1))
        result, report = analyze(cli, session_end_regression)
        assert result.returncode == 2 and not report["complete"], report
        assert any("QPC regressed" in error for error in report["errors"]), report

        duplicate_end = fixture(root, "duplicate-end")
        mutate_jsonl(duplicate_end, lambda rows: rows.insert(-1, dict(next(
            row for row in rows if row["event"] == "epoch_end"))))
        result, report = analyze(cli, duplicate_end)
        assert result.returncode == 2 and not report["complete"], report

        gap_path = fixture(root, "gapped", gaps=(42,))
        result, gapped = analyze(cli, gap_path)
        assert result.returncode == 2
        assert not gapped["complete"] and not gapped["coverage"]["continuous"]
        assert gapped["coverage"]["gaps"][0]["gap_qpc"] == 42

        dropped_path = fixture(root, "dropped", dropped=True)
        result, loss = analyze(cli, dropped_path)
        assert result.returncode == 2
        assert not loss["complete"] and loss["loss"]["dropped"] == 1

        contention_path = fixture(root, "contention", gaps=(0,), contended=True)
        result, contention = analyze(cli, contention_path)
        assert result.returncode == 2 and not contention["complete"]
        assert contention["loss"]["dropped"] == 2 and contention["loss"]["contended"] == 2

        unclosed_health = fixture(root, "unclosed-health", contended=True)
        trace = Path(str(unclosed_health)[:-6] + "-epoch-1-segment-1.jsonl")
        mutate_jsonl(trace, lambda rows: rows.pop())
        result, health = analyze(cli, unclosed_health)
        assert result.returncode == 2 and not health["complete"]
        assert health["loss"]["dropped"] == 1 and health["loss"]["contended"] == 1

        mismatch_health = fixture(root, "mismatch-health", contended=True)
        mutate_jsonl(mismatch_health, lambda rows: next(
            row for row in rows if row["event"] == "segment_end").update(contended=0))
        result, health = analyze(cli, mismatch_health)
        assert result.returncode == 2 and any("contended mismatch" in error for error in health["errors"])

        for event, field in (("epoch_end", "degraded"), ("segment_end", "io_error")):
            bad_health = fixture(root, "bad-health-" + field)
            mutate_jsonl(bad_health, lambda rows: next(
                row for row in rows if row["event"] == event).update({field: True}))
            result, health = analyze(cli, bad_health)
            assert result.returncode == 2 and not health["complete"], (field, health)

        for number, (event, field, value) in enumerate((
                ("epoch_start", "epoch", []),
                ("segment_end", "segment", {}),
                ("segment_start", "segment_file", []),
                ("epoch_end", "reason", []),
                ("epoch_end", "degraded", []),
                ("segment_end", "io_error", []),
                ("segment_end", "contended", {}),
                ("segment_start", "gap_qpc", {}),
                ("session_end", "qpc", []),
                ("epoch_start", "event", []))):
            malformed_path = fixture(root, "malformed-" + str(number))
            mutate_jsonl(malformed_path, lambda rows: next(
                row for row in rows if row["event"] == event).update({field: value}))
            result, malformed = analyze(cli, malformed_path)
            assert result.returncode == 2 and not malformed["valid"] and not malformed["complete"]
            assert any("malformed" in error for error in malformed["errors"]), malformed

        incomplete_path = fixture(root, "incomplete", incomplete=True)
        result, interrupted = analyze(cli, incomplete_path)
        assert result.returncode == 2
        assert not interrupted["complete"]
        assert interrupted["coverage"]["open_epochs"] == [1]
        assert any("no closing footer" in error for error in interrupted["errors"])
    print("test_autonomous_session_report: PASS")


if __name__ == "__main__":
    main()
