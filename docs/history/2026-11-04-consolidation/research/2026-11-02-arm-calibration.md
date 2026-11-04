# Arm calibration flow and respawn rebind-stability — code-grounded findings (2026-11-02)

Scope: primary sources only (`src/core`, `tests`, contracts). Field notes
(MODLOG/MODDING_PLAN) are cited as claims and checked against code. No builds/tests run.

## 1. End-to-end calibration flow as implemented

1. **Sampling entry.** `CalibrationSample` (`src/core/calibration.cpp:340-399`)
 grabs the session epoch/yaw on any space change (`:346-357`), updates the
 preview (`SetPreview`, `:78-100`), then branches: setup requested → capture
 path; otherwise → background refinement (`:380-384`).
2. **One-shot capture.** `CalibrationCapture::Feed` (`src/core/calibration_math.h:118-227`)
 holds a 1 s two-trigger stable window (drift budget 8 cm / cos .9763,
 `calibration_math.h:114-116`), estimates anatomy from head-relative vectors
 (`CalibrationEstimateAnatomy`, `:79-106`; per-side asymmetry later clamped to
 ±3 cm by `CalibrationReachForScale`, `:46-55`), and averages controller quats.
 On completion the worker saves atomically and bumps identity:
 `completed.revision=++g_generation`, live=saved profile, `++g_recenter`
 (`calibration.cpp:286-291,392-397`). Profile validity is gated by
 `CalibrationValidProfile` (`calibration_math.h:12-38`).
3. **Background refinement.** `RefineFromStableWindow` (`calibration.cpp:102-193`)
 runs only when `InputCalibrationRefinementSafe() && presentation_ready`
 (`:381`) — i.e. no theater/menu/capture, sticks near zero, hands slow
 (`src/core/input.cpp:1728-1760`, flag at `:864-866`). It demands a neutral
 region (`InNeutralRegion`, `:68-77`), 1 s / 20 samples inside 8 cm and
 quats within cos .9763 (`:150-162`), averages quats (`:163-168`), refuses
 >45° jumps into `refinement_review_needed` (`:180-184`), otherwise slerps
 8 % toward target capped at 2° (5° unaligned) per pass and re-stamps
 `alignment_reference_yaw=session_yaw` (`:185-192`). Note: refinement mutates
 `g_live_profile` **without** bumping `revision`.
4. **Mapping/fit.** `ResolveCalibrationMapping` (`src/core/calibration_fit.h:84-190`)
 combines head and reach anchors (±25 % clamp, `:146-163`), stamps
 `model_revision` (`:168-171`) and `calibration_revision=profile.revision`
 (`:186`), and refuses unless a model revision exists when anatomy is used
 (`:110`). Camera publishes it with `calibration_revision` (`src/core/camera.cpp:569,728-729`).
5. **Arm consumption.** `ArmIkTick` (`src/core/arm_ik.cpp:1578-1770`) snapshots
 `targets.calibration=CalibrationCurrent()` (`:1608`) and refuses when the
 mapping revision/world-trim disagree (`:1609-1610`); the solver refuses when
 `CalibrationCurrent().revision != targets.calibration.revision`
 (`:437,:816`), rechecks at commit (`:837-856`), and stamps both revisions on
 publication (`:681-682`). `PublishedHandsCurrent` rechecks revision, epoch,
 sequence, freshness (`:1406-1417`). `CalibrationRecenter` bumps only the
 recenter counter, never the profile (`calibration.cpp:328`); input stick
 recenter calls `XrRecenterTheater`, not calibration (`src/core/input.cpp:1473-1475`).
 Roomscale/camera consumers also key on `calibration_revision`
 (`input.cpp:690,697,731,957,993,1003`; `camera.cpp:406-411`).
6. **Neutral-hand basis.** Per side, from the *frozen* `g_source_reference`
 bind row expressed in the bind-root frame and re-expressed in the heading
 frame (`arm_ik.cpp:1628-1659`); `NeutralHandBasisCache::Resolve` keys on
 **(calibration_revision, rig_revision)** (`src/core/arm_pose_math.h:331-353`).
 `ResolveNeutralHandBases` assigns only `NativeBind` or `TposeFallback` —
 never `MirroredNativeBind` (`arm_pose_math.h:295-317` vs enum `:264-268`).
 Grip follows `ArmPoseGripTarget` (`:374-399`) with the rigid hand→weapon
 offset (`ArmPoseWristForGrip`, `:405-431`) and a per-side roll
 (`arm_ik.cpp:735-737`).
7. **Rig lifecycle.** `ReadRig` early-returns when model/flver/entries/data match
 (`arm_ik.cpp:225-226`); a real change bumps `rig.revision=++g_rig_revision`,
 clears reach/stature/source/geometry, invalidates hands, resets torso/pole
 (`:339-348`). `ArmIkResetCalibration` invalidates hands + resets torso/grip
 (`:1427-1431`).

## 2. Ranked hypotheses for imperfect calibration + respawn VDot failure

**H1 — Refinement converges slowly in a possibly stale yaw frame (top for
"never quite perfect").** 2°/pass cap with 8 % blend (`calibration.cpp:186-188`)
 needs many still seconds that `InputCalibrationRefinementSafe` rarely grants
 in play; quats are averaged in *session*-yaw frame (`:133`) while
 `ArmIkTick` applies `neutral_controller` under `alignment_reference_yaw` +
 live camera reference (`arm_ik.cpp:1667-1689`). Any session-yaw vs camera
 drift is a permanent wrist yaw bias; the 45° gate (`:180-184`) then freezes
 further correction until a new hold.
**H2 — One-sided T-pose fallback bakes a permanent twist (top for left/right
 asymmetry).** A side whose frozen bind row is invalid gets its own T-pose
 fallback while the other uses its native bind (`arm_pose_math.h:308-312`;
 `arm_ik.cpp:1714`). This is exactly the diagnosed "palm up, shield flat"
 shape (MODLOG `:7530-7538` — confirmed in code; behavior unchanged, only
 logging added at `arm_ik.cpp:1723-1747`). The ≤12° `grip_roll_correction_deg`
 (`:735-737`) cannot correct a ~90° basis error.
**H3 — True rig rebind has an untested fallback window (top respawn risk).**
 A genuine new rig clears `g_source_reference_valid` (`arm_ik.cpp:341`), so
 `native_neutral_valid` is false and both hands fall back to T-pose until the
 source refreezes (`:561-573`) — a visible wrist snap. The respawn test does
 **not** cover this: it swaps in an identical copy (`rebound_entries`), so
 `ReadRig` early-returns (`:225-226`), no revision bumps, and the cache
 trivially returns the old bases — `VDot>.999` (`tests/test_arm_pose_native.cpp:758-785`)
 passes without exercising invalidation at all.
**H4 — Stale MODLOG mirror claim.** "Mirror the left neutral from the right"
 (MODLOG `:157-167`) is absent from the tree: `ArmIkTick` resolves each side
 from its own bind (`:1638-1659`) with explicit never-mirror comments
 (`:1712-1714`); `MirroredNativeBind` is dead (`arm_pose_math.h:295-317`,
 `arm_ik.cpp:1726-1727`). Do not implement mirror on that note's authority.
**H5 — Revision-blind live refinement.** Refinement mutates the live profile
 without bumping `revision` (H1, `calibration.cpp:185-192`), so revision-keyed
 caches and `PublishedHandsCurrent` (`arm_ik.cpp:1416`) cannot observe it —
 benign today (consumers re-read the profile each tick) but any future
 revision-keyed consumer of `neutral_controller` will go stale.
**H6 — Test precision overstates the guarantee.** The respawn check compares
 normalized per-axis columns at `.999` (`test_arm_pose_native.cpp:777-781`);
 with the cache hit it asserts stability, never correctness, of the basis.

## 3. Discriminating experiment

Run one fixture variant of the respawn test with a **genuinely different**
 rig (altered entries content/address so `ReadRig` bumps `g_rig.revision` and
 clears `g_source_reference_valid`), tracking fixed: if per-axis VDot across
 the rebind drops below .999, H3 is the live respawn defect (fallback window);
 if it holds through invalidation + refreeze, the rebind path is sound and H1
 (refinement frame/convergence) is the remaining imperfection. Headset
 counterpart: read the one-shot `ArmIK: … neutral hand from …` lines
 (`arm_ik.cpp:1745-1746`) — `T-pose fallback` on exactly the bad hand confirms H2.

## 4. Not verified

- No builds/tests run (parent owns verification); all behavior claims are
 static. Whether live bind rows are valid, what the headset log lines say,
 and whether session yaw equals the play-time camera reference need a headset run.
- MODLOG "respawn regression failed before this cache" (`:92-95`) predates this
 tree — pre-cache code not examined. `Load`-path `revision=++g_generation`
 (`calibration.cpp:278`) vs save path ordering under concurrent refine untested.
- `NATIVE_LOCOMOTION_CONTRACT.md` / `NATIVE_MELEE_CONTRACT.md` contain no
 calibration/arm claims (checked). MODDING_PLAN lanes name calibration-room
 and neutral-hand lifecycle as open headset items (`:22-31`, lane F `:252`).
