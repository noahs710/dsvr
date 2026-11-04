# State-driven autopilot for DSVR sim testing

Date: 2026-11-04
Status: approved by user (approach: state-driven autopilot; proof bar: enemy HP drop in trace; location: tools/hmd/)

## Problem

Live validation runs 17-22 were slow and flaky:

- Screenshot steering cost 20-60 s per frame, and the sim preview flakes ("Restoring the view") after mouse interaction.
- Fixed sleeps wasted minutes per run (boot waits, screenshot waits).
- Restore once ground ~50 minutes in `TraceInventory` over accumulated traces (fixed separately in candidate r11: head/tail reads).
- Combat reads stayed ambiguous: H (light attack) produced no visible swing and no contacts except once (run 21: combat_total=42 with 2 distinct-actor contacts); target HP reads are gated (`not_admitted_before_call`, `unqualified_actor_type`).

## Design

One Python driver, `tools/hmd/autopilot.py`, runs the whole loop on state oracles instead of pixels.

### Oracles (all poll-based, 200 ms cadence, hard deadlines)

- Log oracle: tail `DSVR.log` for `CombatHealth` (live player), `InputBridges` (bridge gates), `XInputRoute` (merged buttons).
- Trace oracle: tail the active `DSVR-trace-*.jsonl` for `camera_entity_matrix` (player/camera position), `native_combat*` records, and the closing `footer` (combat counters).
- Shot oracle: the sim screenshot request protocol, used only at two menu checkpoints (dialog, main menu).

### Phases (fail fast, keep evidence, print elapsed per phase)

1. `check` / `launch`: existing protected transaction (`tools/run_combat_capture.ps1`), candidate r11 or newer, short budgets (soak 240 s, menu 300 s, window 30 min).
2. `wait-player`: poll the log oracle until a live player is reported. No screenshots.
3. `menu`: E presses gated on the two shot checkpoints (improper-quit dialog, main menu Continue).
4. `walk`: closed-loop legs toward the gate waypoint; after each leg, read position from the trace oracle and correct yaw with preview mouse-look. No per-step screenshots.
5. `combat`: drop `DSVR-trace-go-<pid>.request`, firebomb volley (R = use item, proven: count 10 -> 9) plus H taps; watch footer counters as the capture closes.
6. `verdict`: run `tools/vr_trace.py` plus the improved HP attribution (below). PASS only when a paired distinct-actor contact shows target `hp_after < hp_before`. Otherwise UNVERIFIED with the read-reason histogram. Never pass on counters alone.
7. `close`: `-Mode Finish`, WM_CLOSE to the hidden game window, wait for process exit, `-Mode Restore`. Print Restore wall time.

### HP attribution fix (in `analyze`)

Current sampling in `tools/vr_trace.py` reports target `not_admitted_before_call` / `unqualified_actor_type`, so paired contacts cannot show HP loss. The autopilot's analyzer resolves each distinct-actor contact's target HP from identity/motion records nearest the contact on both sides (admission may land after the call), and reports per-contact source/target HP with the blocking reason when attribution is impossible. If existing traces still cannot attribute, the verdict stays UNVERIFIED and names the observer-side admission work as the follow-up; it does not weaken the proof bar.

### Offline inner loop

`autopilot.py analyze <trace>` runs the verdict on saved traces (40+ in the game dir) in seconds. Analyzer changes iterate offline; live runs only mint new scenarios.

### CLI

- `autopilot.py plan` — print phases and oracles, no side effects.
- `autopilot.py analyze <trace.jsonl>` — offline verdict JSON.
- `autopilot.py run [--candidate PATH] [--run-dir build/verification/auto-menu-YYYYMMDD-N]` — full transaction; writes `verdict.json` into the run dir.

### Speed rules (baked in)

- No fixed sleep over 5 s except the capture window itself.
- Every wait is a poll with a deadline; every phase logs elapsed seconds.
- Target: full live run under 10 minutes wall; offline analyze under 1 minute.

## Non-goals

- No new native code in the game process (no in-mod bridge; that was the rejected approach).
- No change to the protected transaction semantics (Check/Launch/Finish/Restore stay as-is).
- No weakening of the proof bar to counters or visuals.

## Verification for this change

- `autopilot.py plan` prints the phase list.
- `autopilot.py analyze` on the run-21 contact capture (combat_total=42) and on a zero-contact control capture (run 19/22): verdicts must differ appropriately (UNVERIFIED-with-reasons vs NO-CONTACT), in seconds.
- First live `run` (when the user resumes live work) must print phase times with Restore in seconds-to-minutes, proving the r11 Restore fix end to end.

## Implementation notes (2026-11-04, analyzer)

- `analyze` is implemented against the real event schema (`native_combat_contact` / `_identity` / `_diagnostics` / `_context` / `_motion` / `_health_write`, paired by thread/seq/object). Player side comes from `native_combat_self_excluded` records, falling back to the PlayerIns vtable (`combat_module_base` + 0x13251F0). Only identity-paired contacts feed HP evidence; enemy damage taken by the player is reported separately and never passes.
- A missing footer means an incomplete capture, not unusable evidence: contacts still decide the verdict, counters stay null, and the result carries `complete: false`. MALFORMED is reserved for files with no parseable records.
- Verified offline: run-21 contact capture -> UNVERIFIED (`unqualified_actor_type` x2, `not_admitted_before_call` x2, 0.5 s); run-22 control -> NO_CONTACTS; the retained Oct-28 capture (`build/verification/combat-health-writer-20261028/`) -> PASS via the same 44->31 EnemyIns drop the repo analyzer reports. Synthetic coverage: `tests/test_autopilot_analyze.py` (ctest `autopilot_analyze`).
- Observer admission is not the blocker for real enemy hits. RTTI over the shipped EXE shows exactly four classes carry the type-1 getter (`base+0x339F30`) at vtable+0x10: `ChrIns`, `EnemyIns`, `PlayerIns`, `ReplayGhostIns`. The Oct-28 admitted target is an `EnemyIns`. Run-21's contact target was therefore not a character instance at all; loosening `ReadContactActor` would read HP offsets off unrelated objects and could fabricate drops, so it stays as is. Open follow-up (native, needs its own candidate): record unadmitted actors' vtables in the identity record so failed contacts name what was actually hit.
