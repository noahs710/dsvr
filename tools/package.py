#!/usr/bin/env python3
"""Package DSVR binaries + maintained docs. Never includes game files.

Two modes:
  Formal (default): mint an immutable ``DSVR-<version>`` candidate. Refuses
  to replace any existing stage folder or zip.
  Latest (``--latest``): refresh the single overwritable dev test package
  ``DSVR-latest``. Staged via a temp folder and swapped in only after the
  payload and manifest are complete, so a failed run keeps the previous
  test package byte-for-byte.

Usage:
  python tools/package.py --build build --out dist --version 0.1.0
  python tools/package.py --build build --out dist --latest [--ctest-log build/latest-ctest.txt]
"""
import argparse
import hashlib
import json
import re
import shutil
import sys
import zipfile
from datetime import datetime, timezone
from pathlib import Path


def sha256_of(p: Path) -> str:
    h = hashlib.sha256()
    with p.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def write_manifest(stage: Path) -> None:
    lines = []
    for path in sorted(p for p in stage.rglob("*") if p.is_file()):
        lines.append(f"{sha256_of(path)}  {path.relative_to(stage).as_posix()}")
    (stage / "DSVR-SHA256.txt").write_text("\n".join(lines) + "\n",
                                           encoding="utf-8")


def populate(stage: Path, files: dict, version_text: str) -> None:
    stage.mkdir(parents=True)
    for src, name in files.items():
        (stage / name).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, stage / name)
    (stage / "DSVR-VERSION.txt").write_text(version_text, encoding="utf-8")
    write_manifest(stage)


def read_text_flexible(path: Path) -> str:
    """Read UTF-8 or UTF-16 (PowerShell Tee/redirection) text."""
    data = path.read_bytes()
    if data.startswith((b"\xff\xfe", b"\xfe\xff")) or b"\x00" in data[:64]:
        return data.decode("utf-16", errors="replace")
    return data.decode("utf-8", errors="replace")


def parse_ctest_log(path: Path):
    """Return {passed,total,failed} from CTest summary output, or None."""
    try:
        text = read_text_flexible(path)
    except OSError:
        return None
    m = re.search(r"(\d+)\s+tests failed out of\s+(\d+)", text)
    if m:
        failed, total = int(m.group(1)), int(m.group(2))
        return {"passed": total - failed, "failed": failed, "total": total}
    # Zero-failure runs print only "100% tests passed out of N".
    m = re.search(r"tests passed out of\s+(\d+)", text)
    if not m:
        return None
    total = int(m.group(1))
    return {"passed": total, "failed": 0, "total": total}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", required=True, help="CMake build dir")
    ap.add_argument("--out", required=True, help="Output dir for the zip")
    ap.add_argument("--version", default="0.1.0")
    ap.add_argument("--config", default="Release")
    ap.add_argument("--reshade-addon", action="store_true",
                    help="Include optional host adapter (not the ReShade runtime)")
    ap.add_argument("--latest", action="store_true",
                    help="Refresh the overwritable DSVR-latest dev test package")
    ap.add_argument("--ctest-log", default="",
                    help="CTest output to ship as DSVR-latest-ctest.txt (latest mode)")
    ap.add_argument("--doctor", default="",
                    help="Doctor result recorded in verification.json (latest mode)")
    args = ap.parse_args()

    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*', args.version):
        ap.error('--version must be a single safe filename component')
    if args.version == "latest" and not args.latest:
        print("Refusing formal version 'latest'; use --latest for the dev test package")
        return 1

    out = Path(args.out)
    build = Path(args.build)
    cfg = args.config
    repo = Path(__file__).resolve().parent.parent
    files = {
        build / "_deps" / "imgui-src" / "LICENSE.txt": "licenses/ImGui-LICENSE.txt",
        build / "src" / "loader" / cfg / "dinput8.dll": "dinput8.dll",
        build / "src" / "core" / cfg / "DSVR.dll": "DSVR.dll",
        build / "src" / "setup" / cfg / "DSVR-Setup.exe": "DSVR-Setup.exe",
        build / "_deps" / "minhook-src" / "LICENSE.txt": "licenses/MinHook-LICENSE.txt",
        build / "_deps" / "openxr-src" / "LICENSE": "licenses/OpenXR-LICENSE",
        build / "_deps" / "openxr-src" / "src" / "external" / "jsoncpp" / "LICENSE": "licenses/JsonCpp-LICENSE",
        repo / "LICENSE": "LICENSE",
        repo / "third_party" / "README.md": "THIRD_PARTY.md",
        repo / "tools" / "vr_trace.py": "tools/vr_trace.py",
    }
    for doc in ('README.md', 'MODDING_PLAN.md', 'MODLOG.md',
                'docs/ENGINEERING.md', 'docs/TESTING.md'):
        files[repo / doc] = doc
    if args.reshade_addon:
        files[build / "src" / "reshade" / cfg / "DSVR-ReShade.addon64"] = "DSVR-ReShade.addon64"
        files[build / "_deps" / "reshade_sdk-src" / "LICENSE.md"] = "licenses/ReShade-LICENSE.md"
    missing = [str(k) for k in files if not k.is_file()]
    if missing:
        print("Missing build outputs:", *missing, sep="\n  ")
        return 1

    if not args.latest:
        stage = out / f"DSVR-{args.version}"
        zip_path = out / f"DSVR-{args.version}-x64.zip"
        if stage.exists() or zip_path.exists():
            print('Refusing to replace an existing candidate; use a new version:', args.version)
            return 1
        out.mkdir(parents=True, exist_ok=True)
        populate(stage, files,
                 f"DSVR {args.version}\nOffline play only while installed.\n")
        shutil.make_archive(str(zip_path.with_suffix("")), "zip", stage.parent,
                            stage.name)
        print(f"Wrote {zip_path} ({zip_path.stat().st_size} bytes)")
        return 0

    # Latest mode: the only overwritable identity. Build in a temp stage,
    # then refresh the live folder file-by-file: renaming the folder fails
    # whenever a user has it open (Explorer), while per-file replacement
    # succeeds. This refresh is intentionally NOT atomic: a kill or lock
    # mid-loop (attempts below) may leave the live stage mixed (new+old
    # files). The zip is replaced last and verification.json written after,
    # so a failed refresh never claims the new build — the old zip and
    # verification stay the only shippable identity until the next success.
    stage = out / "DSVR-latest"
    zip_path = out / "DSVR-latest-x64.zip"
    tmp = out / ".DSVR-latest.tmp"
    new_zip = out / ".DSVR-latest.new.zip"
    if tmp.exists():
        print("Refusing: another latest packaging run is in progress "
              f"({tmp} exists); remove it only if no packager is running")
        return 1
    built_utc = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    out.mkdir(parents=True, exist_ok=True)
    try:
        populate(tmp, files,
                 f"DSVR {args.version}\n"
                 "latest (dev test package, not a formal release)\n"
                 "Offline play only while installed.\n"
                 f"Built: {built_utc}\n")
        with zipfile.ZipFile(new_zip, "w", zipfile.ZIP_DEFLATED) as zf:
            for path in sorted(p for p in tmp.rglob("*") if p.is_file()):
                zf.write(path, f"DSVR-latest/{path.relative_to(tmp).as_posix()}")
        stage.mkdir(parents=True, exist_ok=True)
        new_names = set()
        for path in sorted(p for p in tmp.rglob("*") if p.is_file()):
            rel = path.relative_to(tmp)
            new_names.add(rel.as_posix())
            dest = stage / rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            for attempt in range(6):  # transient AV/Explorer locks release
                try:
                    shutil.copy2(path, dest)
                    break
                except PermissionError:
                    if attempt == 5:
                        print(f"Locked, cannot refresh: {dest}")
                        raise
                    import time; time.sleep(0.25)  # transient AV/Explorer locks clear fast; 2 s cost ~10 s in the lock-contention test
        for path in sorted((p for p in stage.rglob("*") if p.is_file()),
                           reverse=True):
            if path.relative_to(stage).as_posix() not in new_names:
                path.unlink()
        new_zip.replace(zip_path)
    finally:
        if tmp.exists():
            shutil.rmtree(tmp)
        if new_zip.exists():
            new_zip.unlink()

    ctest = parse_ctest_log(Path(args.ctest_log)) if args.ctest_log else None
    if args.ctest_log and Path(args.ctest_log).is_file():
        # Ship as UTF-8 even when the build host captured UTF-16.
        (out / "DSVR-latest-ctest.txt").write_text(
            read_text_flexible(Path(args.ctest_log)), encoding="utf-8")
    verification = {
        "package": "DSVR-latest",
        "mode": "latest-dev-test",
        "version_label": args.version,
        "built_utc": built_utc,
        "config": cfg,
        "binaries_sha256": {
            name: sha256_of(stage / name)
            for name in ("dinput8.dll", "DSVR.dll", "DSVR-Setup.exe")
        },
        "ctest": ctest,
        "doctor": args.doctor or None,
        "rate_evidence": {
            "requested_fps": None,
            "runtime_period_ns": None,
            "source": "offline build host; no XR session (see DSVR.log at runtime)",
        },
    }
    (out / "DSVR-latest-verification.json").write_text(
        json.dumps(verification, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {zip_path} ({zip_path.stat().st_size} bytes) + DSVR-latest-verification.json")
    return 0


if __name__ == "__main__":
    sys.exit(main())
