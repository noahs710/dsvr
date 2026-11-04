# Operator capture run-book: natural-hit + aim windows (lanes B/C)

Why this exists: the remaining Kinetic work (AttackBehavior dispatch,
hand-directed aim) needs footer-valid native traces the agent is forbidden
to collect alone — no launch, no input injection, no live memory access on
the operator's behalf (MODDING_PLAN.md, TESTING.md). This is the exact
handoff so one live session qualifies both lanes.

## 0. Requalify first (agent, offline, no launch)

Source changed since the last candidate receipt (Slices A/B/C1), so the old
receipt is stale and must not be reused. Before any live capture:

1. `cmake --build build --config Debug` exit 0, `ctest -C Debug` green.
2. `cmake --build build --config Release` exit 0, `ctest -C Release` green.
3. `tools/auto_verify.ps1` Release green + Doctor PASS.
4. Install with the game closed (saves auto-back up to `.dsvr-backups`).
5. `tools/verify_preflight.ps1` — proceed only at BLOCKED=0.

## 1. Operator setup (operator, ~5 min)

1. Steam: Settings → Account → confirm **Offline** preference is set.
2. Keep the game firewall block in place; stay offline for the session.
3. Launch the candidate through Steam yourself; load the fixed scene and
   ready character (same arena/save as prior lane-B runs).
4. Windowed game at a fixed size; say **"live"** when the character stands
   idle in the arena.

## 2a. Melee window (lane B, ~2 min live)

Agent FIRST, game closed (the script hardened since this read `-Mode Run`:
`Run` is now the detached child and refuses with the game open):

`pwsh -NoProfile -File tools/run_combat_capture.ps1 -Mode Launch -Receipt build/verification/melee-<yyyymmdd> -Cases 'natural_hit:20' -WindowMinutes 15`

That spawns the runner and exits. Operator, inside that window: land **one
ordinary unblocked sword hit** (R1, no weapon art, no backstab/riposte,
target not blocking). Then say **"done"**. Agent runs
`-Mode Finish -Receipt <same>` while the game is still live.

Accept only: matching footer-valid trace, correct case timing, confirmed
player/enemy identity, independent outcome evidence (target HP delta).
Anything else → discard, note why in MODLOG, repeat once max per session.

## 2b. Aim windows (lane C, ~3 min live, same session if the game is up)

Same apparatus, two labeled windows (fresh `-Mode Launch` with its own
`-Receipt` per window, same Finish handshake):

- `-Cases 'cast_lockon:20'`: lock onto the target, cast one sorcery at it.
- `-Cases 'cast_free:20'`: no lock-on, face the target, cast once at it.

Prerequisite the agent checks before any Launch: the apparatus pins the
qualified build (source manifest + binary hashes). If sources changed
since the pinned candidate, mint a fresh candidate receipt first (owning
flow: combat-lab rebuild + receipt); Launch on a stale pin is correctly
refused, never bypassed.

One cast per window, then "done". These pair facing vs lock-on aim for the
same spell and bracket the native aim override probe. Do not invent other
labels mid-session; new labels need their own plan entry first.

## 3. Hand back

Agent posts: receipt path under `build/verification/`, footer verdict,
case timing, identity, outcome evidence, and which lanes resume (B2/B3 for
dispatch, C-shape for aim). Operator closes the game; agent runs `-Mode
Restore` and confirms the inventory check passes.

## Circuit breaker

Same failure 3 times → stop for the session, write down what is known,
change approach or ask. Never reuse a stale candidate receipt.
