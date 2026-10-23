"""Check numerical fixture provenance against a retained capture, without DSR.

Keep this independent of the solver: a fixture must not learn to agree with
the implementation it tests. No eval, expected-pose generation or game writes.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--fixture", type=Path,
                        default=Path(__file__).resolve().parents[1] / "tests/native_idle_pose.inc")
    args = parser.parse_args()
    try:
        capture_bytes = args.capture.read_bytes()
        captured = json.loads(capture_bytes)["samples"][0]["model"]
        raw = bytes.fromhex(captured["values"])
        payload = args.fixture.read_text(encoding="utf-8").split("kNativePoseSample0", 1)[1]
        values = [float(value) for value in re.findall(
            r"[-+]?(?:\d*\.\d+|\d+)(?:[eE][-+]?\d+)?(?=f)", payload)]
        if captured["count"] != 80 or captured["space"] != 1 or len(values) != 960:
            raise ValueError("expected an 80-bone model-space palette (960 floats)")
        if struct.pack("<960f", *values) != raw:
            raise ValueError("fixture bytes differ from captured sample 0")
    except (OSError, ValueError, KeyError, IndexError, struct.error) as error:
        parser.exit(1, f"FAIL: {error}\n")
    print("PASS: 960 fixture floats are byte-identical to captured sample 0")
    print("capture_sha256=" + hashlib.sha256(capture_bytes).hexdigest())
    print("palette_sha256=" + hashlib.sha256(raw).hexdigest())
    print("Coverage: numerical pose provenance only; hierarchy and live skinning are separate evidence")


if __name__ == "__main__":
    main()
