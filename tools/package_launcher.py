#!/usr/bin/env python3
"""Build the optional portable Electron launcher against a staged DSVR package."""
from __future__ import annotations

import argparse
import hashlib
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path, PurePosixPath


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


ENGINE_FILES = ("dinput8.dll", "DSVR.dll", "DSVR-Setup.exe")


def manifest_entries(text: str) -> dict[str, str]:
    entries = {}
    for line in text.splitlines():
        parts = line.split(None, 1)
        if len(parts) == 2:
            entries[parts[1].strip()] = parts[0].lower()
    return entries


def verify_engine(package: Path) -> dict[str, Path]:
    manifest = package / "DSVR-SHA256.txt"
    if not manifest.is_file():
        raise FileNotFoundError("package must contain DSVR-SHA256.txt")
    entries = manifest_entries(manifest.read_text(encoding="utf-8"))
    if any(PurePosixPath(name).name.upper().endswith("PLAN.MD") for name in entries):
        raise ValueError("launcher package must not include development plans")
    files = {name: package / name for name in ENGINE_FILES}
    missing = [name for name, path in files.items() if not path.is_file()]
    if missing:
        raise FileNotFoundError(
            "package must contain the Setup engine resources: " + ", ".join(missing)
        )
    for name, path in files.items():
        expected = entries.get(name)
        if not expected:
            raise ValueError(f"package SHA-256 manifest has no entry for {name}")
        if sha256(path) != expected:
            raise ValueError(f"{name} does not match the package SHA-256 manifest")
    return files


def copy_package_resources(package: Path, destination: Path) -> None:
    """Copy and verify every file named by the staged package manifest.

    The destination is rebuilt from scratch: a leftover file from an older
    package (a stale engine DLL, a dev ledger) must never ship beside the
    current engine.
    """
    manifest = package / "DSVR-SHA256.txt"
    entries = manifest_entries(manifest.read_text(encoding="utf-8"))
    root = package.resolve()
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True, exist_ok=True)
    for relative, expected in entries.items():
        posix = PurePosixPath(relative)
        if posix.is_absolute() or not posix.parts or ".." in posix.parts:
            raise ValueError(f"unsafe package manifest path: {relative}")
        source = root.joinpath(*posix.parts).resolve()
        if not source.is_relative_to(root) or not source.is_file() or sha256(source) != expected:
            raise ValueError(f"package resource is missing or has a bad hash: {relative}")
        target = destination.joinpath(*posix.parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)


def verify_launcher_archive(archive_path: Path, package: Path) -> None:
    package_manifest = package / "DSVR-SHA256.txt"
    expected = manifest_entries(package_manifest.read_text(encoding="utf-8"))
    try:
        with zipfile.ZipFile(archive_path) as archive:
            names = set(archive.namelist())
            required = {f"resources/{name}" for name in (*expected.keys(), "DSVR-SHA256.txt")}
            missing = sorted(required - names)
            if missing:
                raise ValueError(
                    "portable launcher ZIP omitted engine resources: " + ", ".join(missing)
                )
            if archive.read("resources/DSVR-SHA256.txt") != package_manifest.read_bytes():
                raise ValueError("portable launcher ZIP has a different engine manifest")
            for name, expected_hash in expected.items():
                digest = hashlib.sha256(archive.read(f"resources/{name}")).hexdigest()
                if digest != expected_hash or digest != sha256(package / name):
                    raise ValueError(f"portable launcher ZIP has an unverified {name}")
    except zipfile.BadZipFile as error:
        raise ValueError("portable launcher output is not a valid ZIP") from error


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--package-dir", type=Path, required=True,
                        help="staged DSVR package containing engine files and manifest")
    parser.add_argument("--out", type=Path, required=True,
                        help="launcher ZIP path")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parent.parent
    launcher = repo / "launcher"
    engine = launcher / "engine"
    output = args.out.resolve()
    package = args.package_dir.resolve()
    try:
        verify_engine(package)
        engine.mkdir(parents=True, exist_ok=True)
        copy_package_resources(package, engine)
        shutil.copy2(package / "DSVR-SHA256.txt", engine / "DSVR-SHA256.txt")
        output.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(["cmd.exe", "/d", "/s", "/c", "npm.cmd run dist"],
                       cwd=launcher, check=True)
        built = list((repo / "build" / "launcher-portable").rglob("DSVR-Launcher.exe"))
        if len(built) != 1:
            raise FileNotFoundError("expected one DSVR-Launcher.exe in the portable build output")
        app_dir = built[0].parent
        resources = app_dir / "resources"
        verify_engine(resources)
        subprocess.run([str(built[0]), "--smoke"], check=True, timeout=120)
        temporary = output.with_suffix(output.suffix + ".tmp")
        with zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED) as archive:
            for file in sorted(path for path in app_dir.rglob("*") if path.is_file()):
                archive.write(file, file.relative_to(app_dir).as_posix())
        verify_launcher_archive(temporary, package)
        temporary.replace(output)
        checksum = output.parent / "DSVR-Launcher-SHA256.txt"
        checksum.write_text(f"{sha256(output)}  {output.name}\n", encoding="utf-8")
        print(f"Wrote {output} ({output.stat().st_size} bytes)")
        print(f"Launcher SHA-256: {sha256(output)}")
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError,
            subprocess.TimeoutExpired) as error:
        print(f"Launcher packaging refused: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
