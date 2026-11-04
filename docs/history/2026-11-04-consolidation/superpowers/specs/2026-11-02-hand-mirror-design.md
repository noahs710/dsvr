# Hand invariance + desktop mirror default-on — design

Date: 2026-11-02. Slices 1+2 only. Slice 3 (restore timing) is measure-only.

## Problem

- Hands rotate when right stick turns. `ArmIkTick` builds `orientation[]`,
  `relative[]`, `neutral_*`, `pole` from live `g_right0/g_up0/g_fwd0`
  (`CameraTrackingFrame` = `VrHeading.BodyBasis()` after `ApplyTurn`).
  Torso uses `TorsoTargetYaw(head vs heading)` so it stays invariant.
  Hands use `heading * local` so they rotate with the stick.
  Rule: stick never moves hands; only physical arm/wrist does.
- Desktop mirror black when `desktop_present=false`. Default is already
  `true`, but there is no in-game toggle. User wants on-by-default + choice.

## Slices

1. Hands: use seed heading (heading at `SeedOnce`/`Recenter`) for all hand
   transforms in `ArmIkTick`. Live heading keeps driving camera/view,
   locomotion basis, torso-relative yaw. Hands freeze relative to torso
   across `ApplyTurn`. Recenter/new space epoch reseeds hands.
   Per user correction 2026-11-02: each arm calibrates from its OWN native
   bind. The `MirroredNativeBind` fallback and the forced left-invalid line
   are removed; a missing bind uses that side's own T-pose fallback.
   Differential proof (old vs new binary, same fixture): new = old·K0 with
   K0 constant across all roll-free states, K0 = the authored left twist
   replacing the mirrored identity; right side bit-identical (control).
2. Mirror: add `Desktop mirror` checkbox in mod menu, wired to existing
   `Config.desktop_present` (save/load already round-trips it). Default stays
   `true`. No change to `skip_desktop` logic in `render.cpp`.

Non-goals: session restore timing (slice 3) stays logging only;
no new avatar, engine, or test framework.

## Components

- `vr_heading.h`: store `seed_yaw_` on `Recenter`, expose `SeedBasis()`.
  `ApplyTurn` changes only `yaw_`, never `seed_yaw_`.
- `camera.cpp`: expose `CameraSeedBasis()` returning seed basis when ready,
  else live basis as fallback (never refuse hands on missing seed).
- `arm_ik.cpp` `ArmIkTick`: use seed basis for `orientation[]`,
  `relative[]` offset, `neutral_*`, `pole`, `native_neutral_hand` calibration
  basis. Keep live heading for `targets.heading_*` used by view/torso.
- `mod_menu.cpp`: checkbox for `g_edit.desktop_present` near theater toggle.
- Tests: extend `test_vr_heading` (seed invariant across turns) and
  `test_config` (mirror default true + round-trip). Existing seams only.

## Data flow

Stick -> `TurnTracker.Feed` -> `g_pending_turn_yaw` -> `CameraApplyFirstPerson`
-> `g_view_heading.ApplyTurn` -> live `BodyBasis` for view.
Hands: grip quat/pos -> `q0_inv` -> seed basis -> root-inverse -> native.
Turn changes live basis only; seed basis path is untouched.

## Error handling

Non-finite/untracked hands still refuse as today. Missing seed falls back to
live basis (current behavior). Recenter/epoch/revision resets seed use.
Mirror toggle applies live via `DiagnosticsRequestConfig`, restart not needed
except eye buffers (unchanged).

## Testing

- New checks fail before fix: same grip quat with yaw 0 vs +45deg gives
  identical hand local orientation/offset under seed basis.
- `CheckNeutralHandBasisSelection`: per-side sources asserted from the
  documented fallback convention (hand-derived literals, no mirroring).
- `CheckIndividualGripRoll` (renamed): base[0] is the left's own bind
  ({0,1,0 / 1,0,0 / 0,0,-1}); turn/roll formula unchanged (side-only roll
  now matches production on both sides).
- Gate: `ctest -R "vr_heading|config|camera_math|arm_pose"` green.
- Pre-existing, out of lane (other tab's in-flight neutral-cache/commit
  refactor): the respawn rebind-stability check fails identically with and
  without this change (dirty-minus-mine control run). Rebind bumps the rig
  revision, the source re-freezes from the rotated palette, and the right
  neutral recomputes instead of being pinned. Left for the owning tab;
  this change does not touch that path.

## Correction 2026-11-02: capsule turn replaces the seed freeze

The user corrected the model: the right stick must turn the entire player
capsule, hands included. The SeedBasis freeze (this doc's core mechanism)
decoupled hands from the capsule and was removed outright:
VrHeading.SeedBasis/seed_yaw_, CameraSeedBasis, the arm_ik override, and
their tests are gone; hands ride the live BodyBasis like the view, torso,
and locomotion. Standing rule, refined: no input moves hands RELATIVE TO
THE PLAYER except arm/wrist motion; capsule turns (stick/recenter) carry
the whole rigid assembly, so an untouched stick still leaves hands driven
only by controllers and arms. Provenance focus gate also relaxed the same
day: in VISIBLE, validity plus freshness suffice (focus required only in
FOCUSED), so a visible-but-unfocused session no longer needs a forced
refocus to clear the waiting room.
