# DSVR: read this before changing a boundary

For future OpenCode v2 sessions, including Strata / Qwen 3.8 Flash Next.
These are field lessons, not claims about any model's abilities. Keep them short
and current. The best shortcut is remembering which shortcut was already false.

## Start where the evidence stopped

1. Read `MODLOG.md`, then the next unmet gate in
   `docs/superpowers/plans/2026-10-21-native-conversion-execution.md`.
2. Inspect `git status --short` and the actual diff. This checkout contains
   valuable unfinished work: never reset it or stage everything indiscriminately.
3. Reproduce one bounded failure. Change one cause. Rebuild before trusting a
   test executable. Leave the exact command, result and next probe in MODLOG.

README owns scope; ENGINEERING owns architecture; TESTING owns protected runs;
VERIFICATION_RUNBOOK owns acceptance. History is evidence, not marching orders.
Do not write another competing roadmap or demand approval already granted by
the current testing policy. Do not delegate unless the user requests it.

## Tempting mistakes to avoid

**Scale once, not until it looks right.** `CalibrationMapping::world_scale`
means native world units per physical metre, not an arbitrary camera zoom.
Head, hands, roomscale and future native eye offsets must consume the same
applied camera snapshot. Orientation vectors stay unit length. An arm-local
`native_reach / physical_reach` gain used to cancel world scale and detach hands
from controllers. Do not bring it back to conceal poor body fit. Automatic
reach-based world/avatar sizing is not independently resizing a skinned mesh.
That requires a verified render owner. Never scale colliders or bone bases as
a substitute; preserve quaternion/translation-only pose corrections.

**Do not mistake posture for stature.** A single controller capture cannot
measure shoulder sockets. Setup makes an explicit anthropometric estimate
from full head-relative controller geometry. An A pose's horizontal hand spread
is not its full arm span. Keep the independent T/A geometry regression, label
estimates honestly, and keep seated floor height out of anatomy. Older saved
profiles remain usable; recommend explicit recapture after estimator changes,
not silent profile deletion or manufactured "measured" values.

**A quiet pose must actually be quiet.** FABRIK starts from fresh native poses,
not last frame's corrected palette. Its bend-plane state is a continuity budget,
not another animation. Identical inputs must reproduce the same output across
duplicate callbacks and millisecond boundaries. Near-opposite directions have
no unique interpolation tangent: choose a deterministic in-plane one and keep
the cap. A tiny floating-point filter drift can accidentally spend another full
budget step. Tests must catch this, not merely check that matrices are finite.

**Green mathematics is not a green game.** Native armor/skinning, attachments,
world feel and crouch collision need their own native/headset evidence. A stubbed
native-adapter test checks our adapter, not DSR's live behavior. Mono in two eyes
is not stereo; a health setter is not melee. Unknown ABI/ownership is a research
gate, never an invitation to invent an offset. Preserve authored naturalness
thresholds; investigate a red assertion rather than teaching it to stay green.

**The decompiler's cliff is not the end of the function.** Native render
consumer `1411CD8A0` has normal allocation/cleanup fallthroughs omitted by
Ghidra's no-return analysis, plus counter polling and a returned value. Check
pinned bytes and PE unwind bounds before trusting an ABI or lifetime. Do not
repair the primary database or replay the outer dispatcher to work around it.
The exact recovered boundary and next counter-owner probe are in
`docs/NATIVE_STEREO_CONTRACT.md`.

## Useful handles

- FABRIK: `src/core/arm_ik_math.h`; rigid palette transport and hand orientation:
  `src/core/arm_pose_math.h`; actual native boundary: `src/core/arm_ik.cpp`.
- Physical measurements: `calibration_math.h`; coherent mapping:
  `calibration_fit.h`; applied snapshot: `camera.h` / `camhook.cpp`.
- Independent native fixture and limits: `docs/ARM_IK_EVIDENCE.md`,
  `tests/native_idle_pose.inc`, `tests/test_arm_ik_naturalness.cpp`.
  Run-7's real source/body palettes are distinct; the current alias gate refuses
  that path. Its serialized `asm.root` is also offset incorrectly. Read the
  evidence note before claiming native retargeting or deleting a refusal guard.
- To check retained capture provenance without rerunning the game:
  `python tools/verify_native_pose_fixture.py build/native-pose-run7-snapshot.json`.
  A missing ignored capture is an evidence gap, not permission to regenerate a
  supposedly independent fixture from the solver's output.
- Run `cmake --build build --config Debug --parallel 4`, then
  `ctest --test-dir build -C Debug --output-on-failure`; repeat for Release.
  `ctest -N` is the source of registered test names. Never copy an old test count.
- Code graph tools may not be loaded. Discover them first; if unavailable, use
  targeted grep/read. Do not substitute native binary tools for source queries.
  A refresh can still retain old line ranges: confirm the returned snippet is
  the named function before trusting it; stale snippets require direct reads.

Leave the next reader a reason and an experiment, not just an instruction.
