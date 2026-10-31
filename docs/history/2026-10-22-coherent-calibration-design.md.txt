# Coherent calibration and FABRIK completion

Approved direction: automatic coherent fit, 22 October 2026. This is a focused
design for execution-guide F1/F3, not another roadmap or a claim of native
qualification. The existing execution guide retains milestone ownership.

## Goal and boundaries

Calibration must make the world, native player body, head and tracked equipment
agree. A camera-height correction and a separate hand-motion gain are not a
coherent fit. Preserve the native actor, animation lifecycle, gameplay collision
and weapon rules. Do not introduce another avatar, scale gameplay collision, or
put scale in the pose palette's rotation bases.

## FABRIK

Keep FABRIK as the production reach solver and the analytic two-bone solve only
as a characterization reference. Preserve exact link lengths, native twist-pair
relationships, authored hinge conventions and separate end-effector orientation.
Fix opposite-direction bend interpolation deterministically: an absent tangent
must not bypass the angular budget. Invalid persistence must not influence a
new solve. Identical inputs must not spend an extra continuity step. Investigate
the native held-pose drift at the adapter boundary, not by widening assertions.
Exercise reachable, folded, extended, cross-body, overhead, opposite-pole and
multi-link chains with bounded work and malformed-input rejection.

## Calibration and fit

Retain physical measurements independently of native models: stature/floor,
reach and shoulder estimate, wrist reference and seated posture. Treat world
scale as native distance per physical metre, with the existing manual setting
as a trim on the automatic fit. Resolve that conversion once and share it at
camera, hand, movement and stereo-eye boundaries; scale positions, never
orientation vectors or the runtime's physical pose data.

Derive automatic fit from validated native geometry and successful physical
calibration, not a guessed actor offset or the animated third-person camera.
Keep seated posture from shrinking the avatar or world. Preserve calibrated
anatomy across recenter, scene changes and tracking recovery; successful explicit
recalibration replaces it atomically. Invalid/incomplete capture retains the
previous profile. Reject stale model/calibration combinations.

Native visual fitting is separate from the metre conversion. Fit native body
origins/chain geometry through the existing calibration/pose owner while keeping
bone bases rigid and attachments coherent. Do not silently stretch tracked hand
displacement to reach a differently sized native arm. Any render-scale write
requires a verified native owner; if that boundary is not established, leave
that path disabled and report the native integration gap explicitly rather than
inventing an offset or claiming complete model scaling.

## Failure and lifetime policy

Non-finite geometry, invalid profiles, singular roots and stale frames refuse
the correction without partially writing the palette. Rebind geometry and reset
transient solver state on actor/model replacement, calibration revision or
tracking-space discontinuity. Snapshot a frame's conversion so a concurrent
setting change cannot mix scales within one published pose.

## Verification

- Rebuild and run the focused IK, naturalness, native-pose, body, calibration,
  camera, movement, stereo and config tests.
- Add integration-boundary regressions for opposite bend directions, repeated
  held poses, shared scale conversion, tall/short/seated measurements, invalid
  captures and model/calibration changes. Do not relax existing naturalness,
  link-length, basis-scale or continuity bounds.
- Run complete Debug and Release suites and record exact results in MODLOG.
- Native rendered armor/equipment fit and subjective world scale remain
  unverified until exercised on the exact candidate under TESTING policy.
  Same-simulation native stereo and collision/crouch qualification remain
  independent open gates; math tests cannot close them.

## Alternatives considered

Manual world and body controls permit fine tuning but add setup and can conceal
inconsistent conversions. Camera-only height plus arm gain is cheaper but is
the mismatch this task removes. Automatic coherent fit with manual world trim
is the selected direction.
