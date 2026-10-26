"""Split MODLOG.md: keep handoff + active sections live, archive the rest.

Keeps (live MODLOG.md):
  - title + all ### entries before '## Current task and candidate'
  - the 'Current checkpoint' summary subsection
  - '## Evidence boundaries', '## Exact resume action', and everything after
Archives to docs/history/2026-10-26-modlog-task-block.md:
  - '## Current task and candidate' body (closed per-slice evidence)
Only file-level links point at MODLOG.md (AGENTS/README/MODDING_PLAN/
package.py/test_package.py), so no anchor repair is needed. Idempotent:
refuses to run if the archive marker already exists in live MODLOG.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODLOG = ROOT / "MODLOG.md"
ARCHIVE = ROOT / "docs/history/2026-10-26-modlog-task-block.md"
MARKER = "<!-- task-block-archived: docs/history/2026-10-26-modlog-task-block.md -->"


def main() -> int:
    text = MODLOG.read_text(encoding="utf-8")
    if MARKER in text:
        print("already archived; refusing to re-run")
        return 1
    lines = text.splitlines(keepends=True)
    idx = [i for i, l in enumerate(lines) if l.startswith("## ")]
    names = [(i, lines[i].strip()) for i in idx]
    print("\n".join(f"{i}: {n}" for i, n in names))
    start = next(i for i, n in names if n == "## Current task and candidate")
    end = next(i for i, n in names if n == "## Evidence boundaries")
    archived = lines[start:end]
    live = lines[:start] + [MARKER + "\n\n"] + lines[end:]
    ARCHIVE.write_text(
        "# MODLOG task-block archive (26 Oct 2026)\n\n"
        "Closed per-slice evidence moved out of the live handoff to cut "
        "per-session context. Historical only; do not resume from here. "
        "Resume from MODLOG.md.\n\n" + "".join(archived),
        encoding="utf-8",
    )
    MODLOG.write_text("".join(live), encoding="utf-8")
    print(f"archived {len(archived)} lines, live {len(live)} lines")
    return 0


if __name__ == "__main__":
    sys.exit(main())
