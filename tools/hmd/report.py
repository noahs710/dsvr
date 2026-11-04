import hashlib
import html
import json
import sys
from pathlib import Path


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def main(root):
    root = Path(root).resolve()
    failures = []
    try:
        receipt = read_json(root / "receipt.json")
        host = read_json(root / "host.json")
        mcp = read_json(root / "mcp-run.json")
        events = [json.loads(line) for line in (root / "layer.jsonl").read_text(encoding="utf-8").splitlines() if line]
    except (OSError, ValueError) as exc:
        print(f"Invalid run evidence: {exc}", file=sys.stderr)
        return 2
    for key, expected in (("state", "complete"), ("host_exit", 0), ("cleanup_ok", True),
                          ("runtime_cleanup_ok", True), ("runtime_registration_unchanged", True),
                          ("runtime_module_loaded", True), ("layer_module_loaded", True),
                          ("process_forced", False), ("mcp_process_exit", 0), ("mcp_process_forced", False)):
        if receipt.get(key) != expected:
            failures.append(f"receipt {key} must be {expected!r}")
    if receipt.get("runtime_name") != "OpenXR Simulator" or not receipt.get("process_override_used"):
        failures.append("pinned OpenXR Simulator process override was not observed")
    if not host.get("ok") or not host.get("cleanup"):
        failures.append("production XR host or OpenXR cleanup failed")
    if host.get("input_sync_failures", 1):
        failures.append("both-hand action coverage has input synchronization failures")
    action_fields = ("trigger_left_samples", "trigger_right_samples", "squeeze_left_samples", "squeeze_right_samples", "primary_left_samples", "primary_right_samples", "secondary_left_samples", "secondary_right_samples", "menu_left_samples", "menu_right_samples", "stick_click_left_samples", "stick_click_right_samples", "stick_forward_samples", "stick_reverse_samples")
    if any(not isinstance(host.get(key), (int, float)) or host[key] <= 0 for key in action_fields):
        failures.append("both-hand action coverage is incomplete")
    for key in ("theater_submits", "mono_submits", "native_submits", "submitted_frames", "grip_pose_samples", "aim_pose_samples", "aim_offset_samples", "hand_motion_samples", "hand_rotation_samples", "active_neutral_samples", "inactive_action_samples", "hands_lost", "hands_recovered"):
        if not isinstance(host.get(key), (int, float)) or host[key] <= 0:
            failures.append(f"host observation missing or zero: {key}")
    if host.get("not_focused_samples") != 0:
        failures.append("host recorded not-focused samples")
    if not isinstance(host.get("max_head_x"), (int, float)) or host["max_head_x"] < 0.05 or not isinstance(host.get("max_head_yaw_component"), (int, float)) or abs(host["max_head_yaw_component"]) < 0.1:
        failures.append("head motion was not observed")
    if mcp.get("verdict") != "PASS" or not mcp.get("cleanup_ok") or mcp.get("failures"):
        failures.append("simulator MCP exercise failed")
    calls = {item.get("tool") for item in mcp.get("tool_calls", [])}
    catalog = set(mcp.get("tool_catalog", []))
    required = {"get_diagnostics", "get_session_state", "set_head_pose", "set_controller_state", "capture_screenshot"}
    if not required.issubset(calls):
        failures.append("MCP XR control/observation coverage is incomplete")
    for event in events:
        if event.get("event") == "submit" and event.get("result") != 0:
            failures.append("OpenXR frame submission failed")
        if event.get("event") == "capture":
            path = (root / event.get("file", "")).resolve()
            if root not in path.parents or not path.is_file() or not event.get("ok"):
                failures.append("captured eye image is missing or outside the run directory")
    if not any(e.get("event") == "finished" and e.get("capture_failures", 1) == 0 for e in events):
        failures.append("OpenXR layer finish record is missing")
    capture_count = sum(e.get("event") == "capture" for e in events)
    if capture_count < 4:
        failures.append("eye capture coverage is incomplete")
    memory = []
    try:
        lines = (root / "memory.csv").read_text(encoding="utf-8").splitlines()[1:]
        for line in lines:
            columns = line.split(",")
            if len(columns) >= 4:
                memory.append((columns[1], float(columns[3])))
    except (OSError, ValueError):
        failures.append("memory samples are invalid")
    if not memory or max((mb for role, mb in memory if role == "host"), default=0) > receipt.get("memory_cap_mb", 0):
        failures.append("host memory sample is missing or exceeded its cap")
    for field, path_field in (("runtime_dll_sha256", "runtime_dll"), ("runtime_manifest_sha256", "runtime_manifest")):
        try:
            if hashlib.sha256(Path(receipt[path_field]).read_bytes()).hexdigest() != receipt[field]:
                failures.append(f"pinned {path_field} hash mismatch")
        except (OSError, KeyError):
            failures.append(f"pinned {path_field} unavailable")
    images = []
    for event in events:
        if event.get("event") == "capture":
            images.append({k: event.get(k) for k in ("file", "frame", "chain", "width", "height")})
    images.extend({"file": x.get("file"), "sha256": x.get("sha256")} for x in mcp.get("captures", []))
    result = {"verdict": "FAIL" if failures else "PASS", "failures": failures, "images": images,
              "visual_review": "PENDING", "runtime": receipt.get("runtime_name"),
              "submitted_frames": host.get("submitted_frames"), "mcp_tool_calls": len(calls),
              "memory_peak_mb": max((mb for _, mb in memory), default=0)}
    (root / "report.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    rows = "".join(f"<li>{html.escape(str(item.get('file')))}</li>" for item in images)
    page = "<!doctype html><meta charset=utf-8><title>OpenXR Simulator HMD report</title><h1>" + result["verdict"] + "</h1><p>" + html.escape(str(result["submitted_frames"])) + " submitted frames</p><ul>" + rows + "</ul><pre>" + html.escape("\n".join(failures)) + "</pre>"
    (root / "report.html").write_text(page, encoding="utf-8")
    print(result["verdict"])
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1]) if len(sys.argv) == 2 else 2)
