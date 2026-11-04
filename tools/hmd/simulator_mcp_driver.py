import argparse
import asyncio
import base64
import hashlib
import json
import os
from pathlib import Path
import time

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


def content_text(result):
    return "\n".join(item.text for item in result.content if getattr(item, "type", None) == "text")


def decode(text):
    try:
        return json.loads(text)
    except (TypeError, ValueError):
        pass
    if not isinstance(text, str):
        return None
    # Some tool replies append trailing log lines after the JSON payload.
    # Fall back to the first balanced top-level object instead of failing.
    depth = 0
    start = None
    in_string = False
    escaped = False
    for index, char in enumerate(text):
        if in_string:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                in_string = False
            continue
        if char == '"':
            in_string = True
        elif char == "{":
            if depth == 0:
                start = index
            depth += 1
        elif char == "}":
            if depth > 0:
                depth -= 1
                if depth == 0 and start is not None:
                    try:
                        return json.loads(text[start:index + 1])
                    except (TypeError, ValueError):
                        return None
    return None


async def run(args):
    out = Path(args.output).resolve()
    out.parent.mkdir(parents=True, exist_ok=True)
    data = Path(args.data_dir).resolve()
    data.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, SIMXR_DATA_DIR=str(data))
    server = Path(args.server).resolve()
    params = StdioServerParameters(command=args.python, args=[str(server)], env=env,
                                   cwd=str(server.parent))
    report = {"schema": 2, "runtime": "OpenXR Simulator 1.7.1 upstream",
              "source": "upstream MCP ray actions incl. sustained button holds (primary=A right, menu=Start, secondary=B right)",
              "data_dir": str(data), "verdict": "FAIL", "failures": [],
              "tool_catalog": [], "tool_calls": [], "captures": [],
              "controller_inputs": [], "cleanup": [], "cleanup_ok": False,
              "soak_samples": [], "menu_navigation": {},
              "soak_requested_seconds": args.soak_seconds}
    index = 0
    held = set()
    async with stdio_client(params) as (read, write):
        async with ClientSession(read, write) as session:
            await session.initialize()
            catalog = await session.list_tools()
            report["tool_catalog"] = [tool.name for tool in catalog.tools]
            required = {"get_diagnostics", "get_head_tracking", "set_head_pose", "set_headset_profile",
                        "capture_screenshot", "hover_ui_at", "click_ui_at", "hold_ui_at",
                        "release_ui_pointer", "scroll_ui_at", "get_frame_info"}
            if not required.issubset(set(report["tool_catalog"])):
                raise RuntimeError(f"upstream MCP tools missing: {sorted(required - set(report['tool_catalog']))}")

            async def call(name, arguments=None):
                nonlocal index
                arguments = arguments or {}
                start = time.monotonic()
                result = await session.call_tool(name, arguments)
                text = content_text(result)
                record = {"tool": name, "arguments": arguments, "is_error": bool(result.isError),
                          "text": [text] if text else [], "images": [],
                          "duration_seconds": round(time.monotonic() - start, 3)}
                for part in result.content:
                    if getattr(part, "type", None) == "image":
                        index += 1
                        raw = part.data if isinstance(part.data, bytes) else base64.b64decode(part.data)
                        ext = "png" if part.mimeType == "image/png" else "jpg"
                        filename = f"mcp-{index:03d}-{name}.{ext}"
                        target = out.parent / filename
                        target.write_bytes(raw)
                        item = {"file": filename, "mime_type": part.mimeType,
                                "sha256": hashlib.sha256(raw).hexdigest(), "bytes": len(raw)}
                        record["images"].append(item)
                        report["captures"].append(item)
                report["tool_calls"].append(record)
                if result.isError:
                    report["failures"].append(f"{name}: {text[:240]}")
                return decode(text), text

            async def capture():
                value, _ = await call("capture_screenshot", {"eye": "both", "timeout": 8.0, "max_width": 1600})
                if not isinstance(value, dict) or not value.get("capture_id"):
                    raise RuntimeError("upstream screenshot did not return a capture_id")
                return value

            async def pointer(name, args_for_call):
                value, text = await call(name, args_for_call)
                if not isinstance(value, dict) or value.get("status") != "input_delivered":
                    raise RuntimeError(f"{name} was not delivered: {text[:4000]}")
                return value

            async def pose_command(hand, pos, trigger=None):
                command = {"hand": hand, "posX": pos[0], "posY": pos[1], "posZ": pos[2]}
                if trigger is not None:
                    command["trigger"] = trigger
                ack_path = data / "command_ack.json"
                try:
                    before = ack_path.stat().st_mtime_ns if ack_path.exists() else 0
                except OSError:
                    before = 0
                (data / "controller_pose_command.json").write_text(
                    json.dumps(command), encoding="utf-8")
                ack_deadline = time.monotonic() + 5.0
                while time.monotonic() < ack_deadline:
                    try:
                        if ack_path.exists() and ack_path.stat().st_mtime_ns > before:
                            ack = json.loads(ack_path.read_text(encoding="utf-8"))
                            if ack.get("command") == "controller_pose" and ack.get("success"):
                                return True
                    except (OSError, ValueError):
                        pass
                    await asyncio.sleep(0.1)
                return False

            try:
                limit = time.monotonic() + 30
                diagnostic = None
                while time.monotonic() < limit:
                    diagnostic, _ = await call("get_diagnostics")
                    if diagnostic and diagnostic.get("runtime_live") and diagnostic.get("session_state") == "FOCUSED":
                        break
                    await asyncio.sleep(.25)
                else:
                    raise RuntimeError("OpenXR Simulator application did not become FOCUSED")
                report["runtime_session"] = diagnostic
                await call("set_headset_profile", {"name": "quest3"})
                await call("set_head_pose", {"x": 0, "y": 1.7, "z": 0, "yaw_deg": 0, "pitch_deg": 0, "roll_deg": 0})
                await call("get_head_tracking")
                baseline = await capture()
                report["baseline_capture_id"] = baseline["capture_id"]

                await call("set_head_pose", {"x": .12, "y": 1.72, "z": -.08,
                                               "yaw_deg": 24, "pitch_deg": -8, "roll_deg": 6})
                moved = await capture()
                report["motion_capture_id"] = moved["capture_id"]
                report["motion_geometry"] = moved.get("geometry", {}).get("head")
                await call("set_head_pose", {"x": 0, "y": 1.7, "z": 0, "yaw_deg": 0, "pitch_deg": 0, "roll_deg": 0})
                await call("get_frame_info")
                await call("get_head_tracking")

                if args.game_dir:
                    game = Path(args.game_dir)
                    old_traces = {p.name for p in game.glob("DSVR-trace-*.jsonl")}
                    started = time.monotonic()
                    deadline = started + args.menu_seconds
                    # NOTE: pose sweep stays OFF while targeting: the upstream
                    # runtime marks captures taken during a sweep untargetable,
                    # so sweep-during-menu provably breaks holds/clicks.
                    # Poses go through the pinned upstream file interface
                    # (controller_pose_command.json) once per hand; the file
                    # only carries ONE hand per command and the runtime's
                    # per-frame keyboard poll unconditionally clears BOTH
                    # file-driven triggers, so a file hold can never assert
                    # both_triggers. Triggers instead go through latched MCP
                    # hold_ui_at presses (button=trigger, one per hand):
                    # ApplyUiPointerButtons re-applies them AFTER the keyboard
                    # poll every frame, so both stay down together for the
                    # ~1 s calibration hold. Holds use origin_mode=hand with
                    # an explicit synthetic surface so the reported grip stays
                    # at the natural file pose (eye-origin aim would glue both
                    # grips to the eye point, span 0.00, and starve the fit).
                    # File poses persist untouched in posOffset, so the live
                    # span stays at the file spread.
                    spreads = {0: (-0.25, -0.25, -0.35), 1: (0.25, -0.25, -0.35)}
                    for pose_hand, pose in (("left", 0), ("right", 1)):
                        confirmed = await pose_command(pose, spreads[pose])
                        report["controller_inputs"].append(
                            {"hand": pose_hand, "spread_m": list(spreads[pose]),
                             "phase": "calibration_pose", "ack": confirmed})
                        if not confirmed:
                            report["failures"].append(f"menu pose {pose_hand} not acknowledged")
                    # Synthetic panel 1 m in front of the neutral head: both
                    # Skip-pixel rays hit it, so hand-origin holds keep the
                    # natural file-pose grips and only steer the aim.
                    panel = {"pose": {"position": [0.0, 1.5, -1.0],
                                      "orientation": [0.0, 0.0, 0.0, 1.0]},
                             "size": [4.0, 4.0]}
                    trace_started = False
                    hold_tick = 0
                    hold_deadline = 0.0
                    screen = None
                    screen_at = 0.0

                    async def fresh_screen():
                        nonlocal screen, screen_at
                        if screen is None or time.monotonic() - screen_at > 20:
                            screen = await capture()
                            screen_at = time.monotonic()
                        return screen

                    async def latch_triggers():
                        nonlocal hold_deadline
                        scr = await fresh_screen()
                        width, height = scr["image_size"]
                        skip = {"x": int(width * .344), "y": int(height * .49)}
                        for hand in ("left", "right"):
                            target = {"capture_id": scr["capture_id"],
                                "x": skip["x"], "y": skip["y"], "hand": hand,
                                "origin_mode": "hand", "surface": panel,
                                "button": "trigger",
                                "dwell_frames": 2, "timeout": 5.0,
                                "command_id": f"dsvrhold{hand}{hold_tick}"}
                            value = await pointer("hold_ui_at", target)
                            held.add(hand)
                            detail = ""
                            try:
                                detail = json.dumps(value)[:500]
                            except (TypeError, ValueError):
                                pass
                            report["controller_inputs"].append({"hand": hand, "button": "trigger",
                                "phase": "calibration_hold", "x": target["x"], "y": target["y"],
                                "response": detail})
                        hold_deadline = time.monotonic() + 8.0

                    await latch_triggers()
                    while time.monotonic() < deadline:
                        if any(p.name not in old_traces for p in game.glob("DSVR-trace-*.jsonl")):
                            trace_started = True
                            break
                        if time.monotonic() >= hold_deadline:
                            hold_tick += 1
                            await latch_triggers()
                        log = game / "DSVR.log"
                        if log.exists():
                            sample = log.read_text(encoding="utf-8", errors="replace")[-16000:]
                            if "saving=1" in sample or "progress=1.00" in sample:
                                break
                        await asyncio.sleep(0.5)
                    for hand in tuple(held):
                        try:
                            scr = await capture()
                            await call("release_ui_pointer", {"capture_id": scr["capture_id"], "hand": hand})
                        except RuntimeError as exc:
                            report["failures"].append(f"menu release {hand} failed: {exc}")
                        held.discard(hand)
                    # Dismissal matrix: sustained gamepad-button HOLDS while
                    # the sim is live. The calibration holds above assert
                    # analog triggers only, and the click loop below uses
                    # LEFT-hand primary (X, momentary), so the game has never
                    # seen a held confirm. Right primary = A, menu = Start,
                    # right secondary = B (src/core/pad_math.h). Hand origin
                    # plus the synthetic panel keeps the file-pose grips, so
                    # only the button state changes. The Skip pixel is reused
                    # because it is a proven-targetable pixel, not because
                    # the ray hit matters: the held BUTTON is the channel.
                    # A fresh capture after every hold records the dialog
                    # state for that exact button.
                    # Run 11 proved the state machine: boot lands on the
                    # title, the FIRST gamepad button triggers the profile
                    # check which raises the improper-quit dialog, and only
                    # a confirm AT the dialog (OK = A) advances. So A must be
                    # pressed at least twice (title, then dialog). Loop A
                    # holds with a capture and trace check after each; the
                    # per-hold screenshots record every transition for the
                    # analyst. B and menu are single attempts at the end.
                    dismissal = (("primary", "right", 4.0),
                                 ("primary", "right", 4.0),
                                 ("primary", "right", 4.0),
                                 ("primary", "right", 4.0),
                                 ("secondary", "right", 3.0),
                                 ("menu", "right", 3.0))
                    for didx, (button, hand, seconds) in enumerate(dismissal):
                        if trace_started:
                            break
                        if any(p.name not in old_traces for p in game.glob("DSVR-trace-*.jsonl")):
                            trace_started = True
                            break
                        try:
                            scr = await capture()
                            width, height = scr["image_size"]
                            target = {"capture_id": scr["capture_id"],
                                "x": int(width * .344), "y": int(height * .49),
                                "hand": hand, "origin_mode": "hand",
                                "surface": panel, "button": button,
                                "dwell_frames": 2,
                                "timeout": 15.0 if button == "menu" else 8.0,
                                "command_id": f"dsvrdismiss{didx}"}
                            value = await pointer("hold_ui_at", target)
                        except RuntimeError as exc:
                            report["failures"].append(f"dismiss hold {button}/{hand} not delivered: {exc}")
                            continue
                        detail = ""
                        try:
                            detail = json.dumps(value)[:500]
                        except (TypeError, ValueError):
                            pass
                        await asyncio.sleep(seconds)
                        after = None
                        try:
                            after = await capture()
                        except RuntimeError as exc:
                            report["failures"].append(f"dismiss capture after {button}/{hand} failed: {exc}")
                        try:
                            rel = await capture()
                            await call("release_ui_pointer", {"capture_id": rel["capture_id"], "hand": hand})
                        except RuntimeError as exc:
                            report["failures"].append(f"dismiss release {button}/{hand} failed: {exc}")
                        report["controller_inputs"].append({"hand": hand, "button": button,
                            "phase": "menu_dismiss", "seconds": seconds,
                            "before": scr.get("capture_id"), "response": detail,
                            "after": after.get("capture_id") if isinstance(after, dict) else None})
                        if any(p.name not in old_traces for p in game.glob("DSVR-trace-*.jsonl")):
                            trace_started = True
                            break
                    if not trace_started:
                        screen = await capture()
                        width, height = screen["image_size"]
                        # The calibration panel offers "Skip - keep my old
                        # measurements" while a saved profile exists; it sits
                        # in the left-eye view (~0.344w, ~0.49h at neutral
                        # head). Center clicks cannot hit it (they land between
                        # the eye views). Try Skip first, then center.
                        skip = {"x": int(width * .344), "y": int(height * .49),
                                "hand": "left", "origin_mode": "eye", "button": "primary"}
                        center = {"x": int(width * .5), "y": int(height * .5),
                                  "hand": "right", "origin_mode": "eye", "button": "primary"}
                        for attempt in range(8):
                            target = dict(skip if attempt < 4 else center)
                            target.update({"capture_id": screen["capture_id"],
                                "dwell_frames": 2, "hold_frames": 2,
                                "timeout": 5.0, "command_id": f"dsvrmainconfirm{attempt}"})
                            try:
                                value = await pointer("click_ui_at", target)
                            except RuntimeError as exc:
                                report["failures"].append(f"menu confirm attempt {attempt} not confirmed: {exc}")
                                await asyncio.sleep(1)
                                continue
                            detail = ""
                            try:
                                detail = json.dumps(value)[:500]
                            except (TypeError, ValueError):
                                pass
                            report["controller_inputs"].append({"hand": target["hand"], "button": "primary",
                                "phase": "menu_confirm", "x": target["x"], "y": target["y"],
                                "response": detail})
                            await asyncio.sleep(1)
                            if any(p.name not in old_traces for p in game.glob("DSVR-trace-*.jsonl")):
                                trace_started = True
                                break
                            screen = await capture()
                            width, height = screen["image_size"]
                    # Release file-driven triggers so nothing stays pressed
                    # into gameplay; poses persist (valid A-pose hands).
                    for pose_hand, pose in (("left", 0), ("right", 1)):
                        spread = (-0.25, -0.25, -0.35) if pose == 0 else (0.25, -0.25, -0.35)
                        released = await pose_command(pose, spread, trigger=0.0)
                        report["controller_inputs"].append(
                            {"hand": pose_hand, "trigger": 0.0,
                             "phase": "menu_release", "ack": released})
                    report["menu_navigation"] = {"elapsed_seconds": round(time.monotonic() - started, 1),
                                                 "trace_started": trace_started}
                    if not trace_started:
                        report["failures"].append("no local-player trace appeared within the menu budget")
                        if args.finish_file:
                            Path(args.finish_file).write_text("finish-v1\n", encoding="ascii")

                if args.soak_seconds and not args.game_dir:
                    await call("enable_pose_sweep", {"enabled": True, "yaw_amp_deg": 18,
                                                       "pitch_amp_deg": 8, "roll_amp_deg": 6, "freq_hz": .25})
                samples = max(0, args.soak_seconds)
                for second in range(samples):
                    if args.stop_file and Path(args.stop_file).exists():
                        report["failures"].append("stop requested")
                        break
                    if second % max(1, samples // 4) == 0:
                        await capture()
                        diag, _ = await call("get_diagnostics")
                        report["soak_samples"].append({"second": second, "frame_count": diag.get("frame_count") if diag else None})
                    await asyncio.sleep(1)
                report["soak_elapsed_seconds"] = len(report.get("soak_samples", []))
            except BaseException as exc:
                report["failures"].append(f"driver error: {type(exc).__name__}: {str(exc)[:500]}")
                raise
            finally:
                for hand in tuple(held):
                    try:
                        screen = await capture()
                        await call("release_ui_pointer", {"capture_id": screen["capture_id"], "hand": hand})
                    finally:
                        held.discard(hand)
                    report["cleanup"].append({"operation": f"release_ui_{hand}", "ok": True})
                await call("enable_pose_sweep", {"enabled": False})
                await call("set_head_pose", {"x": 0, "y": 1.7, "z": 0, "yaw_deg": 0, "pitch_deg": 0, "roll_deg": 0})
                report["cleanup"].append({"operation": "neutral_head_and_pose_sweep", "ok": True})
                report["cleanup_ok"] = not held and bool(report["cleanup"])
                report["verdict"] = "PASS" if report["cleanup_ok"] and not report["failures"] else "FAIL"
                out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
                if args.done_file:
                    Path(args.done_file).write_text("done\n", encoding="ascii")
    return 0 if report["verdict"] == "PASS" else 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", required=True)
    parser.add_argument("--python", default=os.sys.executable)
    parser.add_argument("--data-dir", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--soak-seconds", type=int, default=60)
    parser.add_argument("--menu-seconds", type=int, default=150)
    parser.add_argument("--game-dir")
    parser.add_argument("--stop-file")
    parser.add_argument("--finish-file")
    parser.add_argument("--done-file")
    args = parser.parse_args()
    if args.soak_seconds < 0 or args.menu_seconds < 0:
        parser.error("durations must be nonnegative")
    return asyncio.run(run(args))


if __name__ == "__main__":
    raise SystemExit(main())
