# DSVR

This is the only plan. The existing code and captured evidence are the starting
point. Update the task below in place; record dated results in `MODLOG.md` and
current qualifications in [docs/EVIDENCE.md](docs/EVIDENCE.md).

## Outcome

Make the pinned Windows Dark Souls: Remastered build playable in VR through a
single launcher. The game stays authoritative for combat, movement, AI, and
saves. Work stays offline and releases contain no game assets.

## What works and what does not

The source has an OpenXR session, tracked camera, theater view, calibration,
arm IK code, installer, and launcher. Native stereo scene rendering is not
bound. Arm IK defaults off and only logs. Physical combat bindings only log.
An October trace observed enemy HP 44 to 31 without a footer or valid case
label. A November trace had contacts but no qualified enemy HP read. Neither
proves physical damage. No headset campaign or release pass exists.

The protected runner can stage a disposable game/save transaction and restore
it. Its first autopilot attempt failed before capture and required manual
recovery. The live-run correction (visible game window, menu-before-player
phase order, unhide before any key, foreground-gated input, runner-owned close
and restore) passes offline checks, including the runner's `-Mode SelfTest`.

A 4 Nov headset session ran on SteamVR: 90 Hz, tracked hands, mono per eye, and
a complete trace. The tracked swing did not fire because the player equipment
never bound (`equipment=0`), so no committed IK hands existed. The "Restoring
the view" panel was the camera-provenance recovery room; it now tolerates the
game presenting one display period ahead and names the failing gate. Until the
equipment bind is fixed, the operator cannot produce a tracked-swing result.

## One task now: a native gameplay proof the operator can run

Gate, play, verdict:

1. `pwsh tools/auto_verify.ps1 -SkipPackage -NoLatest -SkipDoctor` builds and
   runs the full Release CTest in about 7 s incremental.
2. Switch the active OpenXR runtime from the simulator to SteamVR (installed)
   and connect the headset; launch the pinned game offline with the trace on;
   make a tracked swing at an admitted enemy.
3. `python tools/hmd/autopilot.py analyze` reads the newest trace — contact
   identity, HP before/after and dropped records — in under a second.

The operator supplies tracked input, headset stereo, comfort and tracking; the
trace supplies exact contact and damage accounting. That pairing meets or beats
a human-only test and is the only path that can qualify the VR claims.

The keyboard autopilot loop is not that test: it sends a game attack key,
bypasses the mod's tracked-swing path, and can only regress the native observer.
The tracked-swing decision exists (`src/core/input.cpp`) but is gated off by
`arm_ik_log_only`; clearing that flag also enables the unproven arm IK native
pose write, so the first live experiment is to prove that write under the
simulator, then the tracked attack. The protected runner's simulator runtime is
the pinned upstream build (SHA-256 `2ebc134a…`, matching `upstream-lock.json`);
the unqualified runtime is the separately registered
`Documents\OpenXR-Simulator` install (`666b2040…`). Do not claim physical melee
from the synthetic loop.

Accept a physical-melee result only with a footer-valid trace, paired
player-to-enemy contact, admitted enemy identity, independent enemy HP loss, and
a `Restored` receipt.

Once this loop is reliable, use it to fix the native melee binding in one
small slice and repeat until the same evidence passes. Stop after three
identical failures to revise the hypothesis from logs and the native hook.

## After that

Bind independent stereo scene rendering, then body/arm alignment and shield
and weapon controls. Finish with comfort and frame pacing, launcher packaging,
and headset campaign acceptance. Each step must work in the real game before
the next begins. Keep the active next step here; do not create another plan.

## Operating rule

Autonomously inspect source, implement, build, analyze saved captures, and run
relevant offline checks. For a live run, use [docs/TESTING.md](docs/TESTING.md).
Preserve the runner's ownership and recovery barriers. Do not claim a result
from source inspection or synthetic input alone. No commit, push, or publication
unless asked.
