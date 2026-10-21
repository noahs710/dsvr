# DSVR execution plan: work without the HMD

18 October 2026. Implemented subset is being wrapped up at the user's request;
remaining tasks stay open.
Companion to the [main execution plan](2026-10-17-dsvr-execution-plan.md).

## Session checkpoint

Latest integration: **81/81 tests, doctor and development package pass** in
`build/no-hmd-final-auto-verify.txt`. Corrected the desktop-readiness namespace
qualifier, missing-recovery-record uninstall refusal, and test boundaries to match
the existing calibration validation contract. Fresh preflight now verifies 13/13
originals (`build/no-hmd-current-preflight.json`), superseding the earlier missing
alternate-root observation below. No saves were restored or changed to obtain it.
The subsequent SteamVR run reached gameplay/calibration at about 32–35 FPS,
with camera/target work the largest measured mod CPU stage. Receipt
`build/steamvr-20261018-run.json` is restored and all 13 originals verified.
Headset comfort/navigation feedback remains pending; 90-Hz performance was not met.

- Baseline sources and binaries retained in `build/no-hmd-baseline`, with hash
  inventories. Fresh preflight confirmed 8/13 prior originals; the alternate
  AppData save root is absent. No native deployment or game run was performed.
- N1: read protection now explicitly rejects execute-only pages. A copy fault
  invalidates both active and suspended parent permission caches. Actual memory
  regressions and nested/thread/capacity/cross-region cases pass; see
  `build/no-hmd-n1-red.txt` and `build/no-hmd-n1-ctest.txt`.
- N2, partial: bounded interval CPU statistics separate log windows from
  since-reset UI totals; generation tags reject samples spanning an explicit
  reset. Labels clarify inclusive camera/host costs. Instrumentation overhead
  and full-scene GPU timing remain open.
- N4, partial: capture preserves left/right lateral reach estimates; double
  accumulation stabilizes boundary samples. Readiness loss resets a pending hold,
  and stopped calibration cannot retain input capture or accept a new request.
  Real manager tests cover persistence, interrupted holds and failed replacement.
- N5, source checkpoint: a separate desktop Present readiness path enables blur
  enforcement without XR/gameplay permissions. Startup reentrancy/failure modes,
  partial blur-hook rollback and durable reinstall/temp-file behavior are tested.
  Protected desktop/rendered-blur acceptance remains open because of preflight.
- N3 short input-edge handling and the remaining calibration/coordinate audit
  have not been completed. These remain next-session tasks.
- Final integrated build/tests/doctor/package evidence is
  `build/no-hmd-final-auto-verify.txt`; only its successful completion qualifies
  that gate. No HMD acceptance or sustained performance gain is claimed here.

## Session outcome

Use the charging window to finish bounded correctness, diagnostics and desktop UX
work, and prepare stronger evidence for the next headset run. The core queue below
is intended to fit one working session; the discovery queue is optional after the
core gates pass. Do not turn this into an attempt to finish every remaining native
renderer/combat binding at once.

Current baseline: 77/77 tests, doctor and latest development packaging pass. The
user confirmed the first-person/IK flashing stopped after scoped memory-query
reuse. NPC filtering and controller-navigation changes pass offline tests but
still need headset acceptance. All protected transactions are restored.

No HMD is required for any task here. Most tasks need only source, fake OpenXR,
memory fixtures or DX11 WARP. Task N5 additionally permits a protected desktop
game run with VR disabled. No waking, wearing or reconnecting the headset is needed.

## Execution order

`N0 baseline -> N1 memory safety -> N2 useful profiling -> N3 input/UX ->
N4 calibration invariants -> N5 blur enforcement -> N6 integrate/package`

If a task reveals a substantial architectural change, record its evidence and
separate follow-up instead of consuming the whole session. Keep unrelated fixes
in separate source checkpoints. With no Git metadata, preserve source snapshots
and hashes. Discover code through the graph first; use text search for logs/configs.

## N0. Preserve and inspect the current checkpoint

Files: `MODLOG.md`, `docs/TESTING.md`, existing build receipts and transaction script.

1. Confirm the game is stopped and every previous transaction says Restored.
2. Verify the latest restoration receipt and save/install hashes before any new run.
3. Snapshot current sources, tests and configuration; record the current DLL/package
   hashes and retain the existing 77-test result as the starting baseline.
4. Refresh the graph only if the indexed source is stale.

**Done when:** a reproducible, recoverable starting point exists. No game files
or personal saves have been changed as part of this preparation.

## N1. Audit the scoped memory optimization at its real use sites

Files: `src/common/memory.*`, `src/core/arm_ik.cpp`, `camera.cpp`,
`tests/test_memory_write_path.cpp`, `test_arm_pose_native.cpp`.

1. Verify each read scope starts after native animation/camera callbacks and ends
   before calling unknown native code or changing relevant page permissions.
2. Check nested scopes, thread isolation, range overflow, cross-region reads,
   cache-capacity exhaustion, guard/no-access pages and exception recovery.
3. Verify only region permission results are reused. Pose, owner, topology,
   equipment and root data must still be freshly read and checked at commit.
4. Add regressions only for uncovered failure modes. In particular, prove that
   a later callback re-queries protection and that one thread cannot inherit
   another thread's permission cache.
5. Extend the actual arm callback regression to ensure NPC/foreign callbacks
   still execute native animation exactly once but perform no local-player
   equipment queries or pose writes. Verify menu/disabled paths skip that work.
6. If a protection/lifetime assumption is unsound, narrow the scope or use fresh
   checks at that boundary; do not weaken ownership checks to improve a benchmark.

**Done when:** focused memory/adapter tests pass, the scope contract is documented,
and fresh-data/mutation refusal coverage remains intact. This proves correctness
and reduced query counts, not a new live FPS result.

## N2. Make the next performance comparison interpretable

Files: `cpu_frame_timer.h`, `gpu_frame_timer.h`, `render.cpp`, `mod_menu.cpp`,
existing trace analysis tools and `build/e2-*-native.log`.

1. Finish the timing-boundary audit: identify overlapping scopes, omitted polling,
   local-player-only measurements and native-scene GPU time that is not measured.
2. Correct misleading labels where necessary. The current native-camera interval
   includes our restoration, and the host-submit interval excludes host polling.
3. Add bounded interval statistics or snapshot deltas so loading, calibration,
   Home and gameplay are not blended into one since-launch average. Handle reset
   and empty intervals without negative counts or invalid rates.
4. Reuse existing trace/frame data for percentiles where available. If per-frame
   samples are needed, use a bounded buffer and drain outside the hot callback.
   Do not invent p95/p99 from five-second FPS summaries.
5. Measure timer/publication overhead in an offline harness; keep query polling
   nonblocking and memory use bounded. Do not add a flush or GPU wait for profiling.
6. Produce a concise comparison record from retained logs: before/after arm-stage
   costs, sample windows, menu/scene limitations and missing measurements.
7. Prepare the next fixed-scene capture procedure: native, mono without headlook,
   headlook with IK off, headlook with IK on. Keep graphics/resolution fixed and
   reset statistics between segments. Do not execute its headset segments now.

**Done when:** the next run can isolate remaining CPU work and distinguish it from
XR waits and native-scene GPU work. No 72/90-Hz acceptance claim is made offline.

## N3. Finish desktop-testable input and onboarding behavior

Files: `mod_menu.*`, `input.cpp`, `pad_math.h`, `test_input.cpp`, `test_mod_menu.cpp`.

1. [x] Reproduce the observed short-click problem in the actual menu input seam.
   Mouse/key presses that begin and end between render polls must not disappear.
   Prefer bounded queued Win32 edges, with focus-loss release and input capture.
   Completed 19 Oct: `MenuWindowProc` queues keyboard and mouse-button edges to
   render-thread ImGui input. Close paths clear external input and schedule a
   render-thread `ClearEventsQueue`/`ClearInputKeys`/`ClearInputMouse` before the next
   `NewFrame`. `mod_menu_render` injects a deferred click, closes/reopens the actual
   menu, and verifies it cannot leak into the reopened session; red before the fix,
   green after. Full CTest is 81/82 due the unrelated `test_body_pose.cpp:329` yaw assertion.
2. [x] Verify A/trigger selection, B/back, stick navigation and release-to-rearm across
   Home, calibration and theater. The calibration hold trigger must never activate
   a UI control or leak an attack when setup closes.
   Completed offline 19 Oct: fake-runtime coverage asserts left-stick x/y, right-hand
   A/B and trigger selection across Home/calibration, trigger capture, theater D-pad
   routing, and neutral-release rearming after close. Focused Release `input` test
   passes. Physical HMD comfort/navigation acceptance remains pending.
3. [x] Keep native D-pad conversion limited to a known menu/theater context. Do not
   map gameplay movement to item switching. Record native inventory auto-detection
   as a separate binding problem until a reliable menu-state source is found.
   Completed offline 19 Oct: `test_input` verifies theater stick-to-D-pad navigation
   and analog gameplay stick output with D-pad buttons clear. Inventory auto-detection
   remains an explicitly unresolved binding issue.
4. [ ] Exercise actual Apply/Save/Revert and failure feedback with temporary configs;
   verify reopening, focus restoration and bounded command-queue failure handling.
5. Inspect panel registration limits/duplicate IDs and ensure extension callbacks
   are not invoked under the registration mutex. Add meaningful uncovered tests.
6. Render WARP previews at 1280x720, a wide desktop size and a smaller supported
   size. Check selector labels, wrapped instructions, progress/error/cancel states,
   footer reachability and diagnostics/log clipping; fix only observed defects.

**Done when:** actual input/render fixtures pass and previews are visually checked.
These are desktop/backend results; controller comfort/readability in the HMD remains
pending. No claim of complete native inventory navigation.

## N4. Strengthen calibration lifecycle and coordinate guarantees

Files: `calibration.*`, `calibration_math.h`, `camera.*`, `arm_ik.cpp`, existing
calibration/camera/native-pose tests; coordinate contract in `docs/ENGINEERING.md`.

1. Exercise the real calibration manager with recorded synthetic tracking packets:
   launch prompt once, completed capture, death/load/suspension cycles, explicit
   recalibration, cancellation, failed save and successful atomic replacement.
2. Assert active measurements/revision stay unchanged until a successful replacement.
   Recenter changes reference/heading, not reach, neutral wrist or saved profile.
3. Test repeated timestamps, trigger jitter, tracking loss, non-finite samples and
   invalid neutral quaternions at the existing capture seam.
4. Audit the consistency of sample acceptance and saved-profile validation ranges.
   Add a round-trip regression before changing any mismatched bounds.
5. Document every current transform and scale application: physical space, heading,
   head-relative targets, model root, wrist/grip and weapon-local offsets. Identify
   double-scale/lean risks and the migration seam for one uniform body scale.
6. Preserve measured asymmetry where the existing capture can support it, but do
   not infer shoulder sockets, standing height from a seated floor height, or an
   eye position from the animated Head pivot without evidence.

**Done when:** lifecycle invariants are proven at the real manager/caller seams,
and the coordinate contract separates known measurements from estimates. Full-body
retargeting and world/IPD-scale acceptance are not completed by these tests.

## N5. Close and test VR-disabled motion-blur enforcement

Files: `core.cpp`, `render.*`/startup readiness boundary, `motion_blur.*`,
`blur_files.*`, setup installation code and their existing tests.

1. Reproduce the gap through the actual startup seam: VR disabled currently means
   no accepted VR frame host, so deferred runtime blur hooks never activate.
2. Trace the smallest safe startup-ready route for mandatory blur enforcement.
   Keep blur readiness separate from permissions for XR, camera, IK, input and pacing.
   Do not restore early all-adapter installation that previously broke startup.
3. Implement only after the readiness/ownership route is understood. Test gate
   refusal, observer mode, VR-disabled startup, duplicate/reentrant callbacks,
   partial hook failure and absence of unintended XR/gameplay activation.
4. Exercise installation, reinstall, failed installation and uninstall against
   temporary files. Preserve the first original preference, unrelated INI values
   and recovery receipt; failed restoration must retain recoverable state.
5. Run one protected **desktop-only** test with VR disabled: title/gameplay loads,
   native graphics settings attempt to enable motion blur, runtime enforcement,
   and uninstall restoration. Record getter/setter evidence separately from rendered
   visual proof; compare a reproducible moving-camera scene if available.
6. Isolate/hash saves, retain the offline firewall block, preserve installed files,
   and restore/hash-check every original afterward. Stop only the exact tracked PID.

**Done when:** mandatory blur enforcement works without granting VR rights and
reversible file behavior passes. If readiness or rendered blur cannot be proven,
retain the regression/evidence and leave that acceptance item explicitly open.

## N6. Integrate, document and package the completed subset

1. Run focused checks after substantive changes and the full suite at integration.
   Run configure/build, CTest and doctor through `tools/auto_verify.ps1`.
2. Review changed caller paths against the approved product requirements and
   confirm no quality reduction, stale-memory shortcut or unintended native hook.
3. Update MODLOG, TESTING, ENGINEERING and the main checklist with exact results,
   source/build identity and remaining native/HMD qualification. Mark only the
   completed subparts, not whole E1/E2/E3/E9 milestones prematurely.
4. Refresh the overwritable development package after green gates. Check archive
   contents/hashes and required configuration/docs. Do not publish a formal release.
5. Leave the game stopped, all transactions restored and the next headset test
   candidate/config and comparison procedure ready.

**Done when:** source, tests, documentation and development package agree about
what works and what still requires native/headset proof.

## Optional discovery after the core queue

Choose one bounded investigation, based on which next implementation it unblocks:

- **Forearm geometry:** map one ordinary and one mixed-weight arm mesh from retained
  native/GPU evidence to local-owned draw resources, palette/weights, index ranges
  and generation lifetime. Deliver a coverage/evidence record. No asset edits,
  shared-NPC mutation or unproven render hook.
- **Stereo:** inspect the retained RenderDoc capture and pinned executable for a
  view-only replay boundary that excludes simulation/UI and identifies per-eye
  resources/history. Record a candidate ABI/call order or a concrete unresolved
  dependency. Do not relabel repeated mono as stereo/AFR.
- **Combat:** trace one sword collision/damage path and its shield/parry branches
  through the existing read-only corpus. Record owner/lifetime, parameters, stamina
  and defense routing. Runtime hit/block/parry proof remains necessary before
  dispatching outcomes; no HP writes or canned attack substitution.

These investigations can advance without an HMD, but a bounded evidence record
is the session deliverable—not a promise to finish native stereo, forearm-only
rendering or physical combat during the charging window.

## Explicitly deferred to headset availability

Connected invisible-session sleep/wake/reconnect; NPC-fix FPS comparison; physical
overhead/lowered reach and wrist feel; head/helmet/armor coverage in first person;
short/tall/seated scale comfort; two-hand handling; true stereo/AFR depth and motion;
90/72-Hz sustained performance; in-headset menu readability and controls.
