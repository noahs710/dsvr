import hashlib
import html
import json
import sys
from pathlib import Path


def load(path):
    return json.loads(path.read_text(encoding="utf-8"))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main(directory):
    root = Path(directory).resolve()
    failures = []
    try:
        receipt = load(root / "receipt.json")
        cases = load(root / "combat-cases.json")
        mcp = load(root / "mcp-run.json")
        review = load(root / "visual-review.json") if (root / "visual-review.json").exists() else None
        events = [json.loads(line) for line in (root / "layer.jsonl").read_text(encoding="utf-8").splitlines() if line]
    except (OSError, ValueError) as exc:
        print(f"Invalid game-run evidence: {exc}", file=sys.stderr)
        return 2
    sim = receipt.get("simulator", {})
    if receipt.get("state") != "Restored":
        failures.append("transaction did not restore")
    if receipt.get("input_mode") != "OpenXRSimulator" or receipt.get("runtime_registration_unchanged") is not True:
        failures.append("OpenXR Simulator transaction provenance is invalid")
    if receipt.get("simulator_control_exit") != 0 or receipt.get("simulator_control_forced"):
        failures.append("simulator controller did not complete cleanly")
    if sim.get("runtime_name") != "OpenXR Simulator" or not sim.get("process_override_used") or not sim.get("runtime_module_loaded") or not sim.get("layer_module_loaded"):
        failures.append("runtime-passthrough scenario was not observed")
    for hash_key, path_key in (("runtime_dll_sha256", "runtime_dll"), ("runtime_manifest_sha256", "runtime_manifest")):
        try:
            if digest(Path(sim[path_key])) != sim[hash_key]:
                failures.append(f"simulator {path_key} hash mismatch")
        except (OSError, KeyError):
            failures.append(f"simulator {path_key} missing")
    layer = receipt.get("synthetic_layer", {})
    for hash_key, path_key in (("dll_sha256", "DSVR-HmdLayer.dll"), ("manifest_sha256", "DSVR_hmd_lab.json")):
        try:
            if digest(Path(layer.get("directory", "")) / path_key) != layer[hash_key]:
                failures.append(f"telemetry layer {path_key} hash mismatch")
        except (OSError, KeyError):
            failures.append(f"telemetry layer {path_key} missing")
    if layer.get("scenario") != "game-smoke-simulator-v1":
        failures.append("telemetry scenario is not the simulator passthrough case")
    finished = next((e for e in events if e.get("event") == "finished"), None)
    summary = finished.get("input_summary", {}) if finished else {}
    if not finished or finished.get("capture_failures") != 0:
        failures.append("simulator telemetry has no clean finish record")
    action_rows = {(a.get("name"), a.get("hand")): a for a in summary.get("actions", [])}
    for action in ("trigger", "squeeze", "thumbstick", "primary", "secondary", "menu", "stick_click"):
        for hand in ("left", "right"):
            row = action_rows.get((action, hand), {})
            if row.get("calls", 0) <= 0 or row.get("active", 0) <= 0:
                failures.append(f"missing {action} action coverage for {hand}")
            if action in ("trigger", "squeeze") and row.get("nonzero", 0) <= 0:
                failures.append(f"nonzero simulator-driven trigger/squeeze missing for {hand}")
    pose_rows = {(p.get("name"), p.get("hand")): p for p in summary.get("poses", [])}
    for name in ("grip_pose", "aim_pose"):
        for hand in ("left", "right"):
            pose = pose_rows.get((name, hand), {})
            if pose.get("samples", 0) <= 0 or pose.get("max_displacement", 0) < 0.05:
                failures.append(f"moving {name} missing for {hand}")
    head = summary.get("head", {})
    if head.get("samples", 0) <= 0 or head.get("max_x", 0) < 0.05 or abs(head.get("max_yaw_component", 0)) < 0.1:
        failures.append("moving head pose was not observed")
    starts = [e for e in events if e.get("event") == "started"]
    if not starts or starts[0].get("synthetic_inputs") is not False or starts[0].get("runtime_passthrough") is not True:
        failures.append("runtime-passthrough scenario provenance is missing")
    for event in events:
        if event.get("event") in ("submit", "session") and event.get("result") != 0:
            failures.append("OpenXR runtime returned an error")
    image_events = [e for e in events if e.get("event") == "capture"]
    images = []
    for event in image_events:
        path = (root / event.get("file", "")).resolve()
        if root not in path.parents or not path.is_file() or not event.get("ok"):
            failures.append("eye capture missing or outside run directory")
        else:
            images.append({"file": event["file"], "frame": event.get("frame"), "chain": event.get("chain"), "sha256": digest(path)})
    if len(image_events) < 8 or {e.get("chain") for e in image_events} != {1, 2}:
        failures.append("stereo eye capture coverage is incomplete")
    mcp_images = []
    for capture in mcp.get("captures", []):
        path = (root / capture.get("file", "")).resolve()
        if root not in path.parents or not path.is_file() or digest(path) != capture.get("sha256"):
            failures.append("MCP capture missing or hash mismatch")
        else:
            mcp_images.append({"file": capture["file"], "sha256": capture["sha256"]})
    if len(mcp_images) < 4 or mcp.get("verdict") != "PASS" or not mcp.get("cleanup_ok"):
        failures.append("MCP interaction/capture run did not pass")
    memory_rows = []
    try:
        for line in (root / "memory.csv").read_text(encoding="utf-8").splitlines()[1:]:
            values = line.split(",")
            if len(values) >= 4 and values[1] == "game":
                memory_rows.append(float(values[3]))
    except (OSError, ValueError):
        failures.append("memory soak data is invalid")
    if len(memory_rows) < 601:
        failures.append("memory soak does not cover 10 minutes")
    if memory_rows and max(memory_rows) - min(memory_rows) > 512:
        failures.append("game private memory grew beyond limit 512 MiB")
    planned = cases.get("cases", [])
    if not planned or sum(int(c.get("seconds", 0)) for c in planned) < 600:
        failures.append("declared test window is shorter than 10 minutes")
    visual_review = "PENDING"
    if review is None:
        failures.append("Visual review is missing")
    else:
        visual_review = review.get("verdict", "PENDING")
        hashes = [image["sha256"] for image in images] + [image["sha256"] for image in mcp_images]
        if visual_review != "PASS":
            failures.append("Recorded visual review failed")
        if review.get("image_sha256") != hashes:
            failures.append("Visual review does not identify the current captured images")
    result = {"verdict": "FAIL" if failures else "PASS", "failures": failures,
              "physical_melee_gate": "UNVERIFIED", "visual_review": visual_review,
              "images": images, "mcp_images": mcp_images,
              "memory_soak": {"samples": len(memory_rows), "peak_mb": max(memory_rows, default=0)}}
    (root / "game-report.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    image_list = "".join(f"<li>{html.escape(image['file'])} · chain {image['chain']}</li>" for image in images)
    page = "<!doctype html><meta charset=utf-8><title>DSVR OpenXR Simulator run</title><h1>" + result["verdict"] + "</h1><p>Physical melee: UNVERIFIED. Visual review: " + html.escape(visual_review) + "</p><ul>" + image_list + "</ul><pre>" + html.escape("\n".join(failures)) + "</pre>"
    (root / "game-report.html").write_text(page, encoding="utf-8")
    print(result["verdict"])
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1]) if len(sys.argv) == 2 else 2)
