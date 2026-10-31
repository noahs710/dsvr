# Physical combat: layered observability

Approved direction: 24 October 2026, user selected **Approve layered design**.
This is a focused diagnostic contract within execution-guide milestones B/C,
not a replacement roadmap or a claim that physical combat works.

## Outcome and boundaries

Recover attributable natural combat evidence before enabling tracked native
damage, block or parry. Preserve native gameplay, exactly-once natural dispatch,
signature gates, actor lifetime checks and capture-generation isolation. No
guessed ABI, health writes, fabricated attacks or outcome haptics. Existing
neck/camera changes and unrelated untracked files remain untouched.

## Logging design

- Reuse `vr_trace.cpp` and the existing diagnostic writer; do not replace the
  process-wide logger. Hooks enqueue fixed-size observations without formatting,
  allocation, disk I/O or waiting for a mutex.
- Finish the inherited paired contact/identity records. Preserve actor addresses,
  vtables, handles and all 64 return bits without float precision loss. Pair by
  thread, sequence and record within one capture. Reject ambiguous pairs.
- Add explicit read/refusal diagnostics and separately measured observer/native
  call durations. Unreadable data and actor replacement remain unknown outcomes,
  not misses or zero damage. Raw record-side roles are not proven attacker roles.
- Keep detailed observation bounded to 256 natural calls and 20 seconds. Report
  queue loss, observation limits and incomplete pairs explicitly. Writer-side
  INFO summaries describe capability, coverage and timing; repeated health
  summaries are rate-limited. No per-frame text logging.
- The analyzer reports decoded observations, refusal counts and timing
  distributions (including p95/p99), with evidence limitations. Truncation,
  malformed data, queue loss or missing records cannot certify combat coverage.
  HP/stamina deltas and return bits never certify a native attack contract.

## Qualification

Exercise the actual detour/writer/analyzer seam with full-width identities,
replaced actors, unreadable memory, signature refusal, capture replacement,
exactly-once dispatch, detail limits and queue pressure. Rebuild before running
tests; run fresh Debug and Release suites. Offline timing is instrumentation
cost evidence only, not game/VR performance acceptance.

After diagnostics pass, continue the existing `14032F548` producer investigation
and natural hit/miss/block/parry/invulnerability comparison under TESTING's
protected transaction. Only complete ABI, ownership, simulation phase and
independent outcome correlation permit native integration. If those facts remain
unavailable, leave B/C explicitly open and name the exact missing experiment.
