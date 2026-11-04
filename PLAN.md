# DSVR — the plan

One page that says what this mod is, where it stands, and what happens next.
Everything else is reference or history (see "Where things live"). If a doc
disagrees with this file, this file wins.

## What it is

A native VR conversion of Dark Souls: Remastered (Windows Steam, App 1.03.1 /
build 10943698), offline-only. An injected DLL (`DSVR.dll`) runs an OpenXR
session inside the game: your head is the camera, your arms are tracked, and
your swing is the attack — through the game's own systems, never HP edits or
canned animations.

## The standard

We hold DSVR to the same bar as [ERVR](https://github.com/IlyaMez/EldenringVR)
for Elden Ring: one download, one launcher, one START VR button; native
stereo from each eye's own camera; menus and cutscenes on a big screen;
first-person body with calibrated arm length; motion melee and shield work
fed to the game's own combat; settings in the game's own menu; a players'
guide and a changelog. Honest status against that bar:

| Feature | DSVR today |
|---|---|
| Native stereo, per-eye cameras | Render-host preview live; native two-eye binding not yet proven |
| First-person body, tracked arms | Camera + arm IK live; combat bridges still run log-only |
| Your swing is the attack | Trigger/math complete; native damage binding unproven — **stage 1** |
| Shield blocks by position | Guard math complete; native contact binding unproven — **stage 2** |
| Two-hand, skill, cast aim | Two-hand toggle and channels exist; hilt-grab and cast aim unfinished — **stage 3** |
| Big screen for menus/cutscenes | Theater screen live and exercised; headset verdict pending |
| HUD on its own panel | In-game mod menu renders; separate HUD panel not started — **stage 4** |
| Arm calibration, in-menu settings | Calibration and settings menu live |
| One-button launcher | Setup installer exists; START VR launcher not built — **stage 6** |
| Online | Offline-only, by design |

## The work, in order

1. **Physical melee.** Bind the tracked swing to the game's own attack so a
   real hit lands natively. Done when a protected sim capture shows a paired
   contact with enemy HP dropping (`autopilot.py analyze` → PASS). The
   observer already reads EnemyIns HP (proven 44→31 on a retained capture);
   run-21's unreadable target was not a character instance, so no observer
   loosening — the swing must hit the body.
2. **Defence.** Raised shield blocks and moving shield deflects through
   native systems; same style of trace/log proof.
3. **Combat controls.** Hilt-grab two-handing, skill channel, cast aim,
   weapon wheel. While wiring new channels, fold the motion triggers'
   reset/precedence rules into one owner (the shelved architecture
   candidate 01 lives here now, as implementation hygiene, not a program).
4. **Presentation.** Native stereo binding, theater for menus/loading,
   HUD panel, frame pacing.
5. **Body and comfort.** T-pose arm-length calibration, world scale,
   turning options, in-menu settings.
6. **Launcher and release.** One START VR button over the existing Setup +
   packaging: checks game build and runtime, installs, launches offline.
   Changelog distilled from MODLOG; players' guide; versioned candidates.

Parked, not forgotten: settings-state cleanup and XR frame-ownership
unification (architecture candidates 02/03). They are small deepenings to do
inside the stages above if they bite, not separate efforts.

## How we prove things

- `ctest` for code (125+ tests, including trace and input polling).
- Protected sim transactions (`tools/run_combat_capture.ps1`
  Check/Launch/Finish/Restore) against sealed candidates only.
- `tools/hmd/autopilot.py analyze` renders the combat verdict offline;
  PASS means enemy HP dropped in a paired contact trace.
- No live game run unless the user says go. Every run lands in MODLOG.

## Where things live

- `src/` — the mod. `tests/` — tests. `tools/` — packaging, the capture
  runner, trace analyzers, sim drivers. Nothing else is load-bearing.
- `docs/` — live reference only: ENGINEERING, TESTING,
  VERIFICATION_RUNBOOK, OPERATOR_CAPTURE_RUNBOOK, AUTONOMOUS_HMD_TESTING,
  the five NATIVE_* contracts, ARM_IK_EVIDENCE, ATTRIBUTION,
  SOULSMODDING_REFERENCE.
- `docs/history/` — every superseded plan, spec, and note. Read-only.
- `MODLOG.md` — the only ledger; append-only.
- `build/verification/` — sealed candidates and run evidence; never edited.
- `MODDING_PLAN.md` — legacy 65 KB plan, kept until its owner's uncommitted
  edits land; superseded by this file.
