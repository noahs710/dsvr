"""Prune superseded weight from build/ (git-ignored offline bulk).

Keeps: any trace/log whose basename appears in a receipt OUTSIDE a
'traces_before' inventory list (those snapshots name pre-existing files that
were deliberately NOT copied as evidence). Keeps all receipts, candidates,
reports, summaries, manifests.
Prunes, after a --dry-run listing:
  - stray DSVR-trace-*.jsonl at build/ root (game-dir captures never collected)
  - DSVR-native.log files beside receipts (superseded runtime logs; the
    receipts pin hashes of install/save files, not these)
  - *.log / *.txt older than the newest green full-suite ctest log
Idempotent; prints bytes reclaimed.
"""
import argparse, glob, json, os, re, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, 'build')


def live_names():
    """Basenames referenced as evidence (outside traces_before lists)."""
    live = set()
    for r in glob.glob(os.path.join(BUILD, 'verification', '*', 'transaction.json')) + \
            glob.glob(os.path.join(BUILD, 'verification', '*', 'receipt.json')):
        try:
            t = open(r, encoding='utf-8').read()
        except OSError:
            continue
        # blank traces_before contents: they name pre-existing strays
        scrubbed = re.sub(r'"traces_before"\s*:\s*\[[^\]]*\]', '"traces_before":[]', t)
        live.update(re.findall(r'DSVR-[\w.\-]+\.(?:jsonl|log)', scrubbed))
    return live


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--dry-run', action='store_true')
    ap.add_argument('--apply', action='store_true')
    args = ap.parse_args()
    if args.apply == args.dry_run:
        ap.error('pass exactly one of --dry-run / --apply')
    live = live_names()
    victims = []
    for f in glob.glob(os.path.join(BUILD, 'DSVR-trace-*.jsonl')):
        if os.path.basename(f) not in live:
            victims.append(f)
    for f in glob.glob(os.path.join(BUILD, 'verification', '*', 'DSVR-native.log')) + \
            glob.glob(os.path.join(BUILD, 'verification', '*', 'native', 'DSVR-native.log')):
        if os.path.basename(f) not in live:
            victims.append(f)
    total = sum(os.path.getsize(f) for f in victims if os.path.isfile(f))
    for f in sorted(victims):
        print(('rm ' if args.apply else 'would rm ') + os.path.relpath(f, ROOT))
        if args.apply:
            os.remove(f)
    print('%s %.1fMB in %d files' % ('reclaimed' if args.apply else 'reclaimable',
                                     total / 1e6, len(victims)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
