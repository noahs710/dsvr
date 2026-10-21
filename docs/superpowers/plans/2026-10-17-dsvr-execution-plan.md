# DSVR detailed execution plan

Date: 17 October 2026. Updated 18 October: E0 complete; E1/E2 native acceptance
and E9 controller qualification remain open while the headset charges.

This is the task-level companion to [ENGINEERING](../../ENGINEERING.md), the
[product plan](../../../README.md), and [test policy](../../TESTING.md). It
supersedes the execution order in the 16 October unblock plan. Product direction
stays in README; architectural decisions stay in ENGINEERING; execution evidence
and the current handoff stay in MODLOG. This document tracks remaining tasks.

## 1. Scope, baseline and decisions

Target: Quest 3 with Touch controllers, VDXR Medium, RX 9060 XT 16 GB,
Ryzen 7 7700X, 64 GB DDR5. Keep resolution, graphics settings and refresh rate
fixed within each performance comparison. Default target is 90 Hz; qualify 72 Hz
separately. A requested frame cap is not measured performance.

Last integrated checkpoint: 76/76 offline tests passed in
`build/geometry-pacing-ready-ctest.txt`; doctor passed in
`build/geometry-pacing-doctor.txt`. The draft inactive-session pump and its fixture
were added afterward and are not covered by that result. Native stereo/AFR and
physical damage/block/parry dispatch remain unbound. Full-body retargeting and
forearm-only geometry filtering remain unfinished.

Last native attempt: inactive XR Wait/End calls held up loading for roughly
100–200 ms per cycle, producing about 6.4 FPS. The user closed the game. That was
neither a crash nor a gameplay performance result. Earlier active-headset gameplay
measured about 15–16 FPS; disabling IK alone did not resolve it. New head geometry,
graphics-state, pacing and tracking-lifetime fixes still require native acceptance.

The protected transaction is restored. All 13 original save/install file hashes
match `build/geometry-pacing-restoration-check.json`; no game/test session remains
active. Do not deploy or launch as part of planning.

Fixed product decisions:

- Use the native avatar, armor, hands and equipment. Geometry visibility is
  independent of whether a pose solve succeeds. Never collapse bones to hide skin.
- Run calibration once each launch after gameplay loads; retain the session's
  measurements through deaths, loads and equipment changes. Explicit Recalibrate
  reopens the same instruction screen. Failed/cancelled replacement keeps the
  previous usable profile. A saved previous-launch profile is recovery/cancel state.
- Recenter changes view/tracking reference, not measured reach or wrist offsets.
  Temporary tracking loss and Home/theater must preserve intentional heading.
- Offer explicit Seated/Standing/Roomscale selection. A single seated T-pose cannot
  determine standing height or actual shoulder sockets; label estimates honestly.
- Support-hand attachment requires holding grip near the handle; release detaches.
- Advance gameplay/animation/combat once per native update, regardless of eye count.
- Optimize mono and True Stereo at unchanged visual settings first. AFR is an
  optional temporal tradeoff and must not be presented as equivalent to same-frame
  stereo. Lower-resolution presets remain explicit user choices.
- Home provides the extensible settings/control/debug/logging hub. Immersive native
  UI hiding restores normally in theater. Separate VR HUD rendering is a later
  milestone, not a prerequisite for the requested UI-hiding option.
- Blur suppression applies to every mod installation, including VR-disabled use;
  uninstall restores the original preference. Publishing is outside this plan.

## 2. Execution rules and dependency graph

Main dependency chain:

`E0 baseline -> E1 lifecycle -> E2 profiling -> E3 calibration -> E4 visibility ->
E5 embodiment -> E6 mono/IK optimization -> E7 stereo/AFR -> E8 combat -> E10 release gate`

`E9 UX/setup` is maintained alongside relevant stages and must pass before E10.
Read-only native combat/render discovery may run earlier; native acceptance follows
the chain. Do not introduce simultaneous unqualified rendering and combat hooks in
one headset candidate. Do not concurrently edit a shared integration file.

For each task: record the exact change and expected observation, add a regression
at the actual failure seam where available, run focused checks, then integrate.
Broaden testing when integration changes justify it. Avoid tests that merely
duplicate trivial implementation. For unknown native bindings, the deliverable is
first an evidence record: ABI, owner/lifetime, pinned bytes, caller/order, and a
runtime observation. Unproven bindings stay unavailable; helper tests do not count
as feature completion. Do not repeat the same failed headset experiment without
a changed hypothesis and concrete fix/probe.

Keep stages in small independently reviewable source checkpoints. The workspace
currently has no Git metadata: preserve source snapshots and hashes rather than
claiming commits. Do not create a repository or publish work as a planning side effect.

## E0. Establish the execution baseline

Dependencies: none. Files: `MODLOG.md`, `docs/TESTING.md`,
`tests/CMakeLists.txt`, existing build receipts and protected-run script.

- [x] **E0.1 — Inventory current source.** Separate the verified 76-test checkpoint
  from subsequent draft pump files. Refresh the code graph before new discovery.
  Confirm no active game or unfinished save/install transaction. Preserve a source
  snapshot and candidate hashes before implementing E1.
- [x] **E0.2 — Reproduce the draft's current state.** Build and run the inactive-pump
  fixture in isolation, then the integrated suite. Record the actual test count,
  compile warnings and failures; do not silently reuse the 76-test claim.
- [x] **E0.3 — Prepare evidence conventions.** Give each candidate a source/build
  identifier and record config, runtime, headset refresh/resolution, scene, hook
  status and restoration receipt. Reuse existing logs/scripts; add only the fields
  required by the next experiment.

Exit: a reproducible build state, explicit known failures and recoverable snapshot.

E0 evidence: `build/execution-baseline-20261017-222548.zip` preserves sources.
The draft initially failed to compile because of atomic-bool test operations;
after repair `build/execution-baseline-ctest.txt` records 77/77 passing tests.
The graph was refreshed. Existing candidate hashes/receipts and timing logs are reused.

## E1. Fix inactive-headset startup and lifecycle

Dependencies: E0. Files: `src/core/xr_inactive_frame_pump.h`, `xr.cpp`,
`xr.h`, `xr_stereo_frame.h`, `tests/test_xr_inactive_frame_pump.cpp`,
`test_xr_mono_recovery.cpp`, `test_core_start.cpp`.

- [x] **E1.1 — Audit worker lifetime.** Verify module/code lifetime, event handles,
  allocation failure, shared state lifetime and cancellation. No worker waits,
  joins or OpenXR calls may run under loader lock. No mutex is held across a
  potentially blocking runtime call.
- [x] **E1.2 — Integrate exclusive frame ownership.** While the running session is
  synchronized but invisible, let the worker perform complete zero-layer
  Wait/Begin/End cycles. Keep native Present responsive. The worker must never
  access D3D, swapchains, input samples or tracking publication. For a visible/
  focused session, request worker pause and return without blocking until it is
  idle; then resume the existing graphics frame path with exclusive ownership.
- [x] **E1.3 — Defer session teardown safely.** STOPPING/LOSS/EXIT disable new work
  immediately. Complete/cancel owned frame work in order, wait for worker-idle by
  polling on later callbacks, then end the session and release resources. Handle
  a READY event during deferred shutdown and reject stale session results. Do not
  destroy a session while its worker is inside Wait/Begin/End.
- [x] **E1.4 — Separate visibility from running state.** Invisible sessions must not
  drive the first-person camera, hide the desktop avatar or retain active combat
  input. A brief image timeout must still retain a valid pose until its existing
  freshness limit; actual tracking/focus loss must invalidate input appropriately.
- [ ] **E1.5 — Prove recovery.** Force independently blocked WaitFrame and EndFrame
  in fixtures. Assert native callbacks return without waiting and active graphics
  frames never overlap worker cycles. Cover runtime error, worker-start failure,
  repeated pause/request, restart and teardown. Then run one protected cold start
  with an inactive headset and one wake/reconnect transition.

Exit: loading/title remain responsive with the headset inactive; active VR starts
without a restart or concurrent frame API use; errors return to a usable desktop.
The active VR path must not acquire an extra queued frame of latency.

E1 source evidence: actual mono-entry blocking regression fails before integration
(`build/e1-render-block-before.txt`) and passes afterward. Worker and actual XR/WARP
fixtures cover blocked Wait/End, exclusive visible handoff, deferred STOPPING,
same-handle restart and fault reset. Integrated source: 77/77 PASS in
`build/e1-ready-ctest.txt`; doctor passes in `build/e1-ready-doctor.txt`.
Protected inactive-only native receipt: `build/e1-startup-run.json`, restored.
VDXR returned XR_ERROR_FORM_FACTOR_UNAVAILABLE; the game reached the title at
55–60 FPS and Home rendered. All 13 original save/install hashes match
`build/e1-startup-restoration-check.json`. This qualifies desktop fallback, not
an invisible live-session worker or headset wake/reconnect. E1.5 remains open.
The user subsequently confirmed the HMD connected for future runs; the next
protected receipt is `build/e1-connected-run.json`.

## E2. Measure the remaining performance problem

Dependencies: E1. Files: `cpu_frame_timer.h`, `gpu_frame_timer.h`, `render.cpp`,
`frame_rate.cpp`, `display_rate.h`, `camhook.cpp`, `xr.cpp`, `vr_trace.*`.

Initial connected baseline: `build/e1-connected-native.log`. VDXR created an
active session; loading/calibration ran near 90 FPS, but calibrated gameplay
reproduced 13–15 FPS. Arm retarget averaged about 31 ms per local-player call;
native pose averaged .003 ms, XR submission was about 2–3 ms in those samples.
Copy/blit GPU timing excludes the native scene. Receipt
`build/e1-connected-run.json` is restored; all 13 hashes match
`build/e1-connected-restoration-check.json`. An audit of measured/unmeasured
boundaries is retained in `build/e2-timing-boundaries.md`. Four nested arm-stage
counters separate equipment, validation, solve and commit; 77/77 pass in
`build/e2-probe-ctest.txt`. Native probe receipt: `build/e2-profile-run.json`.
The probe is restored/hash-verified. Solving costs ~0.02 ms versus ~9.7 ms
equipment and ~14.3 ms commit. Short owner-thread read scopes now reuse region
permission checks without caching data or dropping SEH/commit revalidation.
The actual memory fixture fails the query-count assertion without reuse and
passes with it (`build/e2-memory-query-before.txt`,
`build/e2-memory-ready-ctest.txt`, 77/77). Camera apply/restore and arm transactions
use separate scopes. Receipt `build/e2-memory-run.json` is restored/hash-verified.
User confirmed flashing stopped. Initial gameplay samples improved to roughly
33–40 FPS; later samples vary with scene/menu state and do not qualify 72/90 Hz.
An actual adapter regression also exposed equipment reads on unrelated NPC pose
callbacks; the early identity refusal now passes (`build/e2-npc-before.txt`,
`build/e2-npc-ctest.txt`). Its native receipt `build/e2-npc-run.json` is restored:
desktop fallback worked, but VDXR saw no system while the HMD charged. Active
performance qualification of that fix remains pending.

- [ ] **E2.1 — Validate measurement boundaries.** Check inclusive/nested CPU stages,
  correct thread ownership, timestamp overhead and GPU query readiness. Report
  unmeasured gaps explicitly. Preserve nonblocking query collection and bounded
  buffers. Add full-scene GPU measurements at a proven native boundary if needed.
- [ ] **E2.2 — Run a controlled comparison.** Use the same disposable character,
  camera location and graphics settings: native baseline, VR mono without headlook,
  mono with headlook/IK off, then mono with IK on. Reset timing counters between
  segments. Separate loading/warm-up from steady gameplay.
- [ ] **E2.3 — Verify recent fixes natively.** Record limiter-setter frequency,
  physical-origin/heading resets, image timeout/held-image counts and graphics-state
  restoration. The cap must not alternate 60/90 because of sub-60 submission
  cadence. Valid poses must not be discarded solely because an image is late.
- [ ] **E2.4 — Locate missing time.** If measured stages do not explain frame time,
  collect CPU wait stacks/ETW or a scoped GPU capture. Identify the blocking call,
  wait owner, repeated pass or state-cache issue before choosing an optimization.
- [ ] **E2.5 — Publish the baseline.** Record median, p95/p99, achieved cadence,
  reported runtime period, missed/reprojected frames where available, CPU/GPU
  bottleneck and measurement limitations. Compare 90 and 72 Hz separately.

Exit: the dominant cost is attributable and the comparison is repeatable. Initial
acceptance target is at least 98% of requested cadence over a three-minute steady
segment, p95 within 1.1 frame budgets and p99 within two budgets. These are goals,
not promised hardware results. Report any unmet target instead of silently reducing
quality or changing refresh rate. Runtime counters that are unavailable stay unknown.

## E3. Make calibration, coordinates and avatar scale consistent

Dependencies: E1; use E2's trace for native timing. Files: `calibration.*`,
`calibration_math.h`, `camera.*`, `camhook.cpp`, `tracking_frame.h`,
`arm_ik.cpp`, `stereo_math.h`/existing native projection helpers, `mod_menu.cpp`.

- [ ] **E3.1 — Lock lifecycle semantics.** Preserve an immutable active profile
  revision during gameplay. Verify one launch prompt, manual recalibration,
  cancellation, failed save and atomic replacement. Run repeated death/load/
  teleport/equipment/Home/tracking cycles without replacing reach or wrist neutral.
- [ ] **E3.2 — Define one coordinate contract.** Document physical reference space,
  avatar/model space, world units, heading, head origin and weapon-local placement.
  Use one tagged camera/body prediction and one uniform scale. Remove independent
  per-path scale application or double lean offsets when migrating callers.
- [ ] **E3.3 — Derive avatar dimensions.** Qualify rest-pose dimensions and a canonical
  eye-to-head offset; the animated Head pivot is not an eye. Use measured standing
  height when available, otherwise an explicitly estimated arm-span reference.
  Seated floor-to-eye height controls posture/view offset, not estimated anatomy.
  Preserve asymmetric user reach where valid without independently scaling the world
  for each arm. Do not alter native weapon length, damage range or collision rules.
- [ ] **E3.4 — Calibrate orientation and grips.** Validate normalized controller
  neutral quaternions, handedness, mirrored roll and weapon-local offsets. Capture
  only after stable pose/trigger hold; tracking loss or bad samples cannot complete
  setup. Preserve the prior usable profile until the replacement is durably saved.
- [ ] **E3.5 — Qualify numerical and native stability.** Cover seated/standing versions
  of the same arm pose, short/tall reference profiles, repeated recenter and varied
  native spawn headings. For reachable targets, aim for grip error within 2 cm and
  orientation within 5 degrees of the requested transform after intentional saved
  offsets; unreachable poses must clamp anatomically and remain finite.

Exit: headset translation, hands and stereo IPD use the same body scale; no
respawn drift or cumulative wrist offsets; the launch screen clearly communicates
posture, estimates, progress and recovery.

## E4. Finish head and forearm-only geometry filtering

Dependencies: E1/E3. Files: `body_visibility.*`, `presence.cpp`, `arm_ik.cpp`,
native geometry adapter(s) introduced only after the draw/resource seam is proven;
`test_body_visibility.cpp`, `test_arm_pose_native.cpp`.

- [ ] **E4.1 — Qualify the existing head filter.** Verify queue/update ABI and local
  ownership against the pinned executable, then observe filtered face/headgear/hair
  records in gameplay. Test initial creation, armor changes, human/hollow forms,
  death, alternate/primary model changes, theater and tracking loss. Keep geometry
  filtering independent of IK success and controller availability.
- [ ] **E4.2 — Establish geometry coverage.** Check all relevant LOD/depth/color/
  shadow passes and custom/base-model variants. Record exactly which variants are
  supported. Determine whether head shadows can be retained with a pass-specific
  filter; do not promise that behavior without proof.
- [ ] **E4.3 — Prove the mixed-mesh seam.** Find the local arm mesh's native vertex/
  index resources, skin weights, palette mapping and lifetime. Associate them with
  the local actor without modifying shared NPC resources. Use retained native draw
  evidence and an actual callback fixture before installing another hook.
- [ ] **E4.4 — Build forearm geometry once per resource generation.** Keep forearm,
  wrist, fingers and equipment; remove head, torso, upper arms and legs. Classify
  mixed-weight boundary triangles and produce a clean elbow termination. Preserve
  materials/normals and avoid stretching, holes or spikes. Retain original
  resources for theater/disable and invalidate bounded caches on mesh replacement.
  No per-frame GPU readback or rewriting game assets.
- [ ] **E4.5 — Qualify armor families.** Start with one ordinary and one mixed
  upper-arm/clavicle asset, then cover the installed stock armor matrix. The
  discovered 58 mixed-weight arm archives require finer filtering than keeping the
  arm slot. Unknown resources must produce an explicit unsupported status; do not
  expose a universal forearm-only claim until that coverage is achieved.

Exit: head/helmet exclusion is reliable; forearm mode preserves visible tracked
hands/weapons and hides other local geometry without body flashes. Whole-body
hiding cannot be used as a substitute that also removes arms/equipment.

## E5. Retarget a coherent body, not only the arms

Dependencies: E3/E4. Files: `arm_pose_math.h`, `arm_ik.cpp`, `camera.cpp`,
`vr_heading.h`, a focused body-pose math module if necessary, native facing adapter
only after its API is qualified; `test_arm_pose_math.cpp`, `test_arm_pose_native.cpp`.

- [ ] **E5.1 — Specify the body-pose packet.** Carry head position/orientation,
  controller poses, body heading, physical scale, root identity and prediction tags
  together. Resolve the known Upper_Root15 -> Spine16 -> Spine1/Chest17 -> Neck18
  -> Head19 chain and clavicle/twist branches through the validated rig binding.
- [ ] **E5.2 — Implement anatomical upper-body solving.** Distribute torso lean,
  shoulder motion and neck/head rotation through real joints with limits. Transform
  descendants coherently and preserve segment lengths. Test neutral, vertical reach,
  leaning/crouching, one-controller loss and near-singular poses before native writes.
- [ ] **E5.3 — Rebase arm solving on corrected shoulders.** Use the retargeted torso
  as the arm solver input. Remove separately added lean so it is not applied twice.
  Retain elbow-pole continuity, wrist orientation, native finger stance, weapon
  mounts and both primary/support grip constraints.
- [ ] **E5.4 — Resolve pelvis/body turning.** Qualify a native-facing or owned visual
  pelvis/foot-turn policy for snap/smooth turns, standing and walking. An upper-body
  twist must not absorb a 180-degree turn while feet face backward. Preserve native
  locomotion/collision and plausible foot placement. Three tracked points infer
  untracked hips/feet; do not claim physical foot tracking.
- [ ] **E5.5 — Handle native animation transitions.** Apply at the natural post-animation
  point before skinning/equipment copy, once per required native update. Rebind
  stance/model/equipment changes without sampling a new wrist neutral. Define comfort
  behavior for rolls, ladders, knockback, death and exceptional cameras so animation
  does not impose uncontrolled HMD roll/translation.

Exit: head, torso, shoulders, arms and visible lower body remain one plausible
avatar during reaching, looking, turning, walking and respawn. A correct upper-body
prototype alone does not complete this milestone.

Progress (18 Oct, torso slice toward E5.2/E5.3): the pose commit yaw-
retargets the structurally bound Spine -> Chest -> Neck subtree toward the
body heading plus the head's clamped yaw offset (`SolveTorsoYaw`, twist
bound 0.8 rad), and both arms then solve from the corrected shoulders.
Reach scaling guards unmeasurable capture asymmetry at the IK seam.
Offline proof: `body_pose` (40 checks), native torso scenario, full
82/82 CTest + doctor + dev package via `tools/auto_verify.ps1`. Still
open here: headset qualification of tangle/uneven feel, removal of the
separate lean addition, E5.4 pelvis/foot turning, E5.5 animation
transitions, and the canonical eye-to-head offset. Lean is now carried
by the torso (clamped translation) with the grip-side addition removed.
One protected headset attempt (18 Oct 21:20 EDT) produced no
qualification: the XR runtime was unavailable at launch and the game
exited before any session; see MODLOG for the relaunch precondition.

## E6. Optimize measured mono and IK costs

Dependencies: E2/E5. Files: hot paths identified by E2, primarily `arm_ik.cpp`,
`arm_pose_math.h`, `render.cpp`, `xr.cpp`, `render_host.cpp`, timing helpers.

- [ ] **E6.1 — Cache immutable rig work.** Cache validated topology, descendant sets,
  bind dimensions and grip metadata by owner/resource generation. Keep replacement
  and same-address mutation detection. Do not trade guarded ownership for stale
  pointer access. Remove allocations and repeated transforms only where measured.
- [ ] **E6.2 — Reduce synchronization overhead.** Publish coherent snapshots with
  short ownership boundaries; never hold mod locks across native/XR calls. Solve
  once per pose evaluation, not per eye or duplicated hook. Avoid added smoothing
  that creates HMD latency.
- [ ] **E6.3 — Optimize copies, queries and state preservation.** Keep one source
  snapshot per mono frame, asynchronous bounded queries and complete native state
  restoration. Compare a faster context-state path only if feature support,
  predication, bindings, class instances and UAV counters remain correct.
- [ ] **E6.4 — Rerun matched benchmarks.** Use the E2 scene/settings and report gains
  by stage. Confirm wrist/body behavior and memory/resource use during a longer run.
  Resolution reduction and reduced effects are separate optional presets, not
  evidence of a quality-preserving engine optimization.

Exit: a measured improvement with unchanged image settings and no pose/visibility
regression. If native engine or runtime costs still prevent 90 Hz, document the
remaining bottleneck and measured 72 Hz behavior explicitly.

## E7. Bind and optimize True Stereo, then expose AFR/mono modes

Dependencies: E1/E3/E4/E5/E6. Files: `render.cpp`, `xr.cpp`, `xr_stereo_frame.h`,
`stereo_render.h`, `stereo_gate.h`, `afr_policy.h`, existing native view adapters,
`tools/native_stereo.py` and associated tests.

- [ ] **E7.1 — Prove the view-only boundary.** Identify native camera-consumption,
  scene draw dispatch and worker joins. Separate rendering from simulation/input/
  animation/contact work. Capture actual consumed view/projection constants and
  resource identities; retained synthetic contracts alone are insufficient.
- [ ] **E7.2 — Render a correct eye pair.** Bind one prediction/token to two distinct
  targets with asymmetric projections and physical IPD. Advance game state once.
  Use the same committed avatar/weapon pose for both eyes. Restore owned native
  state and cancel/drain the pair safely on any failed eye or replaced resource.
- [ ] **E7.3 — Separate eye-dependent state.** Qualify depth, culling, screen-space
  effects, temporal history, transparencies and UI. Share simulation, pose solving,
  assets, and proven view-independent rendering. Do not share a history/depth buffer
  simply because doing so is faster.
- [ ] **E7.4 — Optimize True Stereo.** Measure CPU submission and full-scene GPU
  costs; reduce repeated work without degrading settings. Consider shared visibility
  from an adequate stereo frustum and reusable passes only after correctness is
  shown. Do not promise single-pass stereo without a supported shader/engine path.
- [ ] **E7.5 — Expose qualified mono modes.** Keep centered immersive mono and readable
  theater/compatibility presentation. Clearly separate user-selected performance
  resolution presets from algorithmic optimizations. Verify transitions do not
  change calibration or duplicate simulation work.
- [ ] **E7.6 — Implement experimental AFR.** Alternate real eye renders with correct
  view identity, frame age and resource generation. Qualify pose/depth handling,
  recovery, moving-hand appearance and latency/judder. Reject stale or mismatched
  eyes; disable on unsupported paths. Label its temporal tradeoff and keep it opt-in.

Exit: captured native/GPU evidence proves two distinct correct views and one game
update; headset depth/occlusion and motion are comfortable. Each exposed mode has
its own measured performance/limitations. A repeated mono source never qualifies
as stereo or AFR.

## E8. Finish native physical combat and realistic weapon handling

Dependencies: E3/E5; integrate after E7's update/render ownership is settled.
Read-only discovery may start earlier. Files: `combat_native.*`, `combat_contact.h`,
`combat_math.h`, `input.cpp`, `arm_ik.cpp`, `weapon_grip.*`, focused native adapters
introduced after their ABI is proven.

- [ ] **E8.1 — Recover a native attack/contact trace.** Follow equipped weapon and
  attack parameters through collision, damage, stamina, poise, reaction and defense.
  Record exact ABI, owners, actor lifetime IDs, scheduling and pinned entry points.
  Observe one native sword hit, shield block and parry before injecting outcomes.
- [ ] **E8.2 — Produce reliable physical motion/contact.** Derive blade/shield volumes
  from committed weapon transforms. Sweep translation and rotation, using bounded
  substeps or continuous tests to prevent tunneling. Exclude walking, stick turns,
  recenter, teleport and tracking spikes from attack energy.
- [ ] **E8.3 — Dispatch native melee once.** Use verified native outcomes and their
  attack/defense/stamina/status rules. Budget continuous contact by target lifetime,
  time and first impact; prevent duplicate eye/render hits and resting damage.
  Native dodge invulnerability and defenses remain authoritative.
- [ ] **E8.4 — Implement shield blocking and parry.** Contact, shield facing and
  coverage determine physical blocking. Qualify the game's parry timing/contact
  semantics rather than using a hand-height shortcut. Confirm stamina/guard break,
  poise and reaction behavior from native outcomes.
- [ ] **E8.5 — Complete two-hand interaction.** Grip near the saved support handle
  attaches; release detaches. Use a rigid weapon solution with believable wrist
  limits and stable primary/support placements. Qualify native two-hand stance/
  stats through a verified adapter, and restore state on release, equipment change,
  death, menu/focus loss or tracking failure.
- [ ] **E8.6 — Add confirmed haptic feedback and breadth.** Prioritize bounded
  hit/block/parry/hurt envelopes per hand, cancel on disable/loss, and attribute
  outcomes correctly. First qualify one sword/shield and one two-handed weapon;
  then test additional weapon families, stance and handedness combinations.

Exit: a native encounter is playable using physical hits, blocks and parries at
multiple update rates. No direct HP writes, button-triggered attack animations,
frame-rate-dependent repeat hits or duplicated stamina/status costs substitute for
the requested system.

## E9. Complete the Home hub, onboarding and installation behavior

Dependencies: maintain alongside E1–E8; finish before E10. Files: `mod_menu.*`,
`diagnostics.*`, `src/common/config.*`, `logging.*`, `native_ui.*`,
`motion_blur.*`, `blur_files.*`, `src/setup` and packaging documentation.

18 October offline checkpoint: 77/77 tests, doctor and development package pass
(`build/e9-latest-auto-verify.txt`). Actual input fixture now proves theater
left-stick D-pad navigation and calibration controller routing; A selects
Home/calibration while the calibration hold trigger remains reserved. Calibration
selector layout was visually checked in `build/e9-calibration-preview.bmp`.
Native inventory/menu auto-detection and headset operation remain unqualified;
E9.1/E9.3 stay open. No game or protected transaction remains active.

- [ ] **E9.1 — Qualify menu operation.** Home and the VR shortcut work across supported
  states. Controller/keyboard navigation, input capture, focus restoration,
  Apply/Save/Revert, DPI/resolution and failed-save recovery are consistent. UI
  commands run through bounded queues, not blocking file/native calls in drawing.
- [ ] **E9.2 — Finish extension and diagnostics contracts.** Validate panel IDs,
  bounded registration/command failures and typed settings. Show real mode/hook/
  tracking/performance availability; support filtered bounded logs, markers and
  export. Unavailable rendering/combat features cannot look functional.
- [ ] **E9.3 — Polish launch/recalibration UX.** Clearly show posture, arms-out pose,
  release/hold progress, saving/error state and the saved-profile cancel action.
  Cover the world while setup is shown; verify gameplay input is held. If native
  pause cannot be proven, state that simulation continues and do not claim a pause.
- [ ] **E9.4 — Verify immersive UI hiding.** Hide native UI only in immersive VR when
  enabled; theater restores it. Home/calibration remain reachable and readable.
  Cover native HUD/menus and loading transitions supported by the actual hook.
  Track separately rendered VR HUD/UI as a later project after core acceptance.
- [ ] **E9.5 — Enforce reversible blur suppression.** Close the VR-disabled/runtime
  graphics-menu gap. Preserve the original preference before enforcing off;
  reinstall keeps the original receipt; failed restore aborts uninstall without
  losing recovery state. Test cached settings and rendered blur, not just getter
  return values. Preserve unrelated configuration and make failed installs recoverable.

Exit: the user can operate/recover the mod in the headset, understand available
features and obtain useful diagnostics; installing/uninstalling restores expected
native configuration.

## E10. Integrated acceptance and development package

Dependencies: E1–E9 complete for declared supported coverage.

- [ ] **E10.1 — Run the lifecycle matrix.** Cold/warm launch, headset absent/inactive/
  active, sleep/wake/reconnect, focus loss, repeated deaths/respawns/loads, manual
  recalibration/recenter, equipment/armor/stance swaps and Home/theater transitions.
- [ ] **E10.2 — Run embodiment/visibility acceptance.** Seated/standing and short/tall
  references, horizontal/overhead/lowered/close-body reach, wrist roll, lean/crouch,
  turning/walking and exceptional animations. Inspect both eyes, weapons, armor
  seams, head clearance and camera stability.
- [ ] **E10.3 — Run render/combat acceptance.** Every exposed render mode at 90/72 Hz,
  moving hands/world, UI visibility and contact outcomes. Test native update-rate
  independence, resting weapons, tracking spikes, target replacement, two-hand
  release and sustained gameplay. Record resource growth and frame-time outliers.
- [ ] **E10.4 — Verify installation recovery.** Reinstall, failed config/calibration/
  receipt writes and uninstall preference restoration. Every native run ends with
  original save/install hashes restored; keep runtime-owned Steam metadata separate.
- [ ] **E10.5 — Build the review package.** Run full Release build, CTest and doctor;
  refresh only the overwritable development package after green checks. Verify
  archive inventory, hashes, licenses and the absence of game/decompiled assets.
  Include measured performance, supported armor/weapon/runtime coverage and known
  limitations. Update README/ENGINEERING/TESTING/MODLOG to describe the same result.

Exit: a reproducible development package with native evidence, protected-run
restoration receipts and no claimed-complete production paths that remain unbound.
Do not publish/upload as part of this gate.

## 3. Verification commands and native-run procedure

Commands below are future execution steps, not commands run while preparing this plan.
Use the existing build directory and its pinned dependency configuration.

Focused checks, as appropriate to the changed stage:

```powershell
cmake --build build --config Release --parallel 6
ctest --test-dir build -C Release --output-on-failure -R 'xr_inactive_frame_pump|xr_mono_recovery|frame_rate_native|display_rate|d3d11_state_guard'
ctest --test-dir build -C Release --output-on-failure -R 'calibration|camera_writes|vr_heading|body_visibility|arm_pose'
ctest --test-dir build -C Release --output-on-failure -R 'stereo|afr|render_host|frame_queue'
ctest --test-dir build -C Release --output-on-failure -R 'combat|weapon_grip|input|native_effects|blur|mod_menu|config|logging'
```

Full offline gate without refreshing the package:

```powershell
.\tools\auto_verify.ps1 -NoLatest
```

After native acceptance and documentation match the source, refresh the development
package through `.\tools\auto_verify.ps1`. Check actual process exit codes and
logs at each dependency; never deploy a failed or partially rebuilt candidate.

For each native experiment:

1. State the specific hypothesis, changed variable, expected observation and stop
   condition. Confirm candidate hashes and relevant offline checks first.
2. Reuse the established protected receipt/watchdog workflow. Verify exact target
   paths, offline block, original save/install inventory and disposable character.
   Preserve the original log. Desktop input authorization already supplied for
   these tests persists; obtain missing headset readiness only when needed.
3. Start with a stationary/desktop-observed phase. Ask for a short headset comfort
   check only after the concrete visibility/lifecycle issue is fixed. Do not repeat
   the prior uncomfortable stretched-body candidate.
4. Capture the minimal log/image/GPU/trace evidence required. End on a freeze,
   unsafe view or failed hypothesis; classify user exit versus crash accurately.
5. Restore and hash-verify originals, restore the original log and update the
   receipt/MODLOG before another candidate or handoff. Never leave an active
   transaction while doing extended source work.

## 4. Discovery risks and explicit decision gates

| Unknown | Required proof | Decision if proof fails |
|---|---|---|
| Inactive XR worker handoff/teardown | Forced-block fixture plus real wake/stop | Keep VR graphics unavailable on that lifecycle path; preserve desktop responsiveness and fix ownership before another headset run |
| Eye anchor/body proportions | Rest-data dimensions and consistent pose mapping | Use a declared tested canonical offset/estimated scale; do not auto-fit from animated head position or seated floor height |
| Mixed forearm geometry/resources | Local ownership, weights, lifetime and clean boundaries | Keep forearm mode unavailable for unsupported coverage; do not restore bone collapse or affect NPCs |
| Native view-only stereo boundary | Captured consumed eye matrices/resources and one simulation update | Keep production mono and mode explicitly unavailable; continue scoped native discovery |
| Eye-dependent postprocessing/history | Correct per-eye native/GPU output | Isolate the affected history/pass before exposing stereo; do not silently sacrifice effects |
| Physical contact/outcome API | Qualified ABI, owners and observed native outcomes | Leave outcome dispatch unavailable; do not substitute HP writes or attack buttons |
| 90 Hz at unchanged quality | Controlled native CPU/GPU/frame-time measurements | Report the bottleneck and actual result, qualify 72 Hz separately, and label optional resolution tradeoffs |

No time estimate is assigned to unproven native bindings. Re-estimate the next
milestone after its discovery gate, rather than promising a completion date based
on helper-test progress.
