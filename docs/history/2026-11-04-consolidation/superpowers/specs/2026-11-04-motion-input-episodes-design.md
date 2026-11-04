# Motion-input episodes: one owner for lifecycle and precedence

Date: 2026-11-04
Status: Draft (grill in progress; decisions settle here as they are made)

## Context

Architecture review 20261104 candidate 01 (top pick, Strong): the motion-input
triggers in `src/core/input.cpp` each hide their own math
(`swing_attack_math.h`, `shield_guard_math.h`, `skill_flick_math.h`), but their
composition stays exposed in the polling caller:

- Five per-side trigger state machines are globals on the polling thread
  (`g_swing_attack`, `g_shield_guard`, `g_skill_flick`, `g_reach`, `g_blade`,
  input.cpp:85-94).
- Reset blocks for those machines repeat at input.cpp:129-133, :878-903,
  :1165-1168, :1195-1199, :1217-1220, :1436-1443, driven by equipment,
  capture, invalid-frame, and timing conditions.
- Collision/precedence rules live in the caller (e.g. skill flick suppressed
  by a firing swing or thrust shape, input.cpp:1270-1300;
  `skill_flick_math.h:14` states this explicitly).

## Goal

Deepen the motion-input module around episode ownership and arbitration:
one place for trigger lifecycle (reset/rearm) and cross-channel precedence,
so callers stop assembling those rules. Preserve the existing input
acquisition seam and manual combat controls. The trigger math headers stay;
this is in-process composition, no new provider hierarchy.

## Constraints

- Native-input previews, not qualified physical damage or shield-contact
  bindings; behavior must not change beyond the consolidation.
- Existing `tests/test_input.cpp` scenarios (defense, attacks, equipment
  reprime, captured input through real polling) are retained.
- This record decides design only; implementation needs a separate request.

## Open decisions

1. Stage-1 scope: which state machines and rules move behind the module.
2. Module interface shape and state ownership (instances vs globals).
3. Reset signalling: explicit caller events vs module-detected conditions.
4. Parameter/config ownership for the trigger tunings.
5. Test surface: how composed behavior is exercised; fate of current tests.

## Scope contract

Unresolved until the interview settles it: artifact boundary, done-means
checklist, and staging. Later stages (if any) return for their own interview.

## Decision log

(none settled yet)
