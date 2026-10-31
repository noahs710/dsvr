#!/usr/bin/env python3
"""Publish a qualified, immutable DSVR package to its existing GitHub tag."""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

_VERSION = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]*\Z")
_OPEN_TASK = re.compile(r"^\s*-\s*\[\s\]\s+[A-K]\d", re.MULTILINE)
_EXPECTED_LANES = {f"{chr(code)}.md" for code in range(ord("A"), ord("K") + 1)}


def open_gates(lane_dir: Path) -> list[str]:
    files = sorted(lane_dir.glob("*.md"))
    names = {path.name for path in files}
    if names != _EXPECTED_LANES:
        missing = sorted(_EXPECTED_LANES - names)
        extra = sorted(names - _EXPECTED_LANES)
        raise ValueError(f"expected lane files A-K; missing={missing}, extra={extra}")
    result = []
    for path in files:
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if not _OPEN_TASK.match(line):
                continue
            if path.stem == "K" and re.match(r"^\s*-\s*\[\s\]\s+K[56]\b", line):
                continue  # K5 publishes; K6 records the resulting package
            result.append(f"{path.name}:{number}: {line.strip()}")
    return result


def open_gate_count(lane_dir: Path) -> int:
    return len(open_gates(lane_dir))


def remote_tag_exists(version: str, runner=subprocess.run) -> bool:
    if not _VERSION.fullmatch(version) or version == "latest":
        raise ValueError("release version must be one safe, immutable label")
    result = runner(
        ["git", "ls-remote", "--exit-code", "--tags", "origin",
         f"refs/tags/v{version}"],
        capture_output=True, text=True, check=False)
    return result.returncode == 0 and bool(result.stdout.strip())


def ctest_passed(path: Path) -> bool:
    data = path.read_bytes()
    if data.startswith((b"\xff\xfe", b"\xfe\xff")) or b"\x00" in data[:64]:
        text = data.decode("utf-16", errors="replace")
    else:
        text = data.decode("utf-8", errors="replace")
    failed = re.search(r"(\d+)\s+tests failed out of\s+(\d+)", text)
    if failed:
        return int(failed.group(1)) == 0 and int(failed.group(2)) > 0
    passed = re.search(r"tests passed out of\s+(\d+)", text)
    return bool(passed and int(passed.group(1)) > 0)


def doctor_passed(path: Path) -> bool:
    data = path.read_bytes()
    if data.startswith((b"\xff\xfe", b"\xfe\xff")) or b"\x00" in data[:64]:
        text = data.decode("utf-16", errors="replace")
    else:
        text = data.decode("utf-8", errors="replace")
    required = ("PASS: pinned build supported", "[OK] Game not running",
                "[OK] DSVR installed")
    failed = re.search(r"(?m)^\s*(?:\[FAIL\]|FAIL:)", text)
    return all(item in text for item in required) and failed is None





def release_command(out_dir: Path, version: str) -> list[str]:
    if not _VERSION.fullmatch(version) or version == "latest":
        raise ValueError("release version must be one safe, immutable label")
    stage = out_dir / f"DSVR-{version}"
    archive = out_dir / f"DSVR-{version}-x64.zip"
    assets = [archive, stage / "DSVR-Setup.exe", stage / "DSVR-SHA256.txt"]
    missing = [str(path) for path in assets if not path.is_file()]
    if missing:
        raise FileNotFoundError("formal release assets missing: " + ", ".join(missing))
    return [
        "gh", "release", "create", f"v{version}",
        *(str(path) for path in assets),
        "--verify-tag", "--fail-on-no-commits",
        "--title", f"DSVR {version}", "--generate-notes",
    ]


def main() -> int:
    parser = argparse.ArgumentParser()
    modes = parser.add_subparsers(dest="mode", required=True)
    gate = modes.add_parser("check-gates")
    gate.add_argument("--lanes", type=Path, required=True)
    gate.add_argument("--version", required=True)
    publish = modes.add_parser("publish")
    publish.add_argument("--lanes", type=Path, required=True)
    publish.add_argument("--out", type=Path, required=True)
    publish.add_argument("--version", required=True)
    publish.add_argument("--ctest-log", type=Path, required=True)
    publish.add_argument("--doctor-log", type=Path, required=True)
    args = parser.parse_args()
    try:
        gates = open_gates(args.lanes)
        if gates:
            print(f"Formal publication blocked: {len(gates)} cockpit prerequisites remain open.")
            for item in gates:
                print(f"  {item}")
            return 1
        if not remote_tag_exists(args.version):
            print(f"Formal publication blocked: pre-push tag v{args.version} is missing; this tool never creates or pushes tags.")
            return 1
        if args.mode == "check-gates":
            print(f"Release prerequisites closed and v{args.version} exists on origin.")
            return 0
        if args.mode == "publish":
            if not doctor_passed(args.doctor_log):
                print("Formal publication blocked: supplied Doctor log does not show a clean supported-build pass.")
                return 1
            if not ctest_passed(args.ctest_log):
                print("Formal publication blocked: supplied CTest log is not a zero-failure run.")
                return 1
        command = release_command(args.out, args.version)
        result = subprocess.run(command, check=False)
        return result.returncode
    except (OSError, ValueError) as error:
        print(f"Release publication refused: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
