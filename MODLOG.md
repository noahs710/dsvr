# DSVR current handoff

[Master plan](README.md) owns direction. [Recon](MODDING_PLAN.md) owns findings;
[engineering](docs/ENGINEERING.md) owns implementation; [testing](docs/TESTING.md)
owns test/release policy. Update these in place; keep this journal short.

## Roomscale body follow — 21 October 2026

User approved native-root roomscale movement as the first embodiment slice. Current
camera source adds tracked HMD translation to the native player anchor; arm/torso
pose commits do not move the actor root. The approved design in `docs/ENGINEERING.md`
requires routing bounded horizontal roomscale intent through the existing XInput
path, preserving DSR root/collision ownership, and reconciling only DSVR-induced root
motion to avoid double-counting the camera offset. No direct root or bone translation.

Read-only Ghidra located `XINPUT1_3.DLL::XInputGetState` at `EXTERNAL:0000000d`,
thunk `0x14082f790`, and six callers. `0x140c9faa0` reads the controller state,
normalizes axes and dispatches mapped actions; `User.Action.MovementStick` string
`0x1414e0ce8` is registered by `0x140e3bb90`. This does not yet prove the exact
player-movement consumer, root-delta attribution, or collision result. Do not enable
roomscale injection until that chain and a protected native correlation are verified.
The read-only damage trace likewise leaves `0x140358460`'s receiver continuation
unresolved (`0x1424c2b5b` … `0x142b7e7e9` → `0x14035862d` / `0x1403d4b6b`); the HP
setter `0x140322910` alone is not a contact ABI. AI `AddTopGoal` remains separate.

Two-hand solver and combat bench are gated by IK/profile/config/fresh-commit state;
source exposes no weapon-slot actions. Current evidence and unresolved follow-ups are
source/Ghidra observations only; no native movement or damage was changed or claimed.

## Physical contact continuation — 20 October 2026

Source fixes, not native combat completion:
- The committed-pose proving ground now takes swing energy from the bound
  physical head/hand packet with head-relative velocities and predicted sample
  time. Native root movement, heading and IK corrections cannot manufacture
  energy. Grip-local -Z maps to world once; the old row/double-root direction
  was wrong. Both hands are sampled before defence, regardless of dominance.
- Bench history resets on missing tracking, menu/calibration, motion disable,
  epoch, actor/model, equipment/stance, calibration revision, scale, dominance
  and prediction discontinuities. Duplicate callbacks retain motion/rehit state.
  Menu/config checks refuse stale diagnostic snapshots.
- Rejected hand teleports previously subtracted head velocity from a zeroed
  sample, creating phantom energy. Only continuous accepted samples now receive
  head correction; changing feed modes reprimes.
- Equipment snapshots recheck actor type, stance owner and stance value before
  publishing IDs. All three mutation fixtures failed before this fix.
- The actual adapter direction regression fails against the saved original
  (`build/weapon-interactions-before.txt`) and passes with the fix. Coverage
  includes walking rejection, swing, duplicates, tracking loss, epoch/menu/
  disable and dominance changes. The head-displacement header now correctly
  describes its diagnostic-only role.

Read-only follow-up reconfirms `0x1403c7520 → 0x1403c4020` with record
actor fields +0x130/+0x138 and type dispatch via vtable +0x10. Type-2 branch
`0x1403c4210` reaches `0x1402f84f0/0x1402f84b0`; one reaches effect/response
code `0x1403e6450`, not a proven HP sink. Type-1 `0x1403c4140` crosses split
code `0x1425ba7dc → 0x1403c4162`; current Ghidra boundaries omit continuations.
Treating that pseudocode as a complete ABI would be invalid. Derived output
stays external at `C:/Tools/DarkSoulsRemaster_De/combat-evidence/weapon-*-20261020.txt`.
Native target extraction, weapon dimensions/axes, damage/stamina/poise dispatch,
incoming attack/block/parry and event haptics remain unbound. The proving ground
uses an assumed .9m blade and a synthetic target/guard proxy; it neither enforces
hand collisions nor applies native outcomes.

Changing to a health-first trace found the concrete setter `0x140322910`:
raw instructions store actor+0x3e8, clamp to +0x3ec, honor +0x524 flags and
native keep-alive conditions. This is a health setter, not a contact/damage ABI,
and remains disconnected. The pinned PlayerIns vtable +0x3e8 points to
`0x140359240 → 0x14280602c → 0x14104c734 → 0x14035925b` on one branch;
its split continuations still need ABI recovery and outcome attribution.
Evidence: external `weapon-health-*-20261020.txt`. REA MCP still reports its
Windows native authority unavailable; read-only headless queries work.

Verification: Release build, 84/84 CTest, doctor and development package refresh
passed, including the final repeat after panel wording/format cleanup
(`build/weapon-interactions-auto-verify-final.txt`). No game or headset run occurred. The panel
now describes its guard timing correctly and calls native combat unfinished.
Original source snapshots: `build/weapon-interactions-baseline/`.

## Head-directed locomotion + native analysis recovery — 20 October 2026

`head_directed_move` now works end-to-end: after a successful follow-camera
update, the game thread publishes the applied HMD forward and freshly validated
native render forward to the render-thread pad mapper. Gameplay left-stick
motion is remapped into native axes only for the same tracking epoch and a
fresh (≤100 ms) snapshot. Invalid/camera-reset/tracking-loss snapshots and
sanctuary navigation retain raw native axes. `test_input` covers transform,
disabled, epoch-mismatch, menu and invalidation paths; `test_camera_writes`
covers publisher identity and native-read refusal. Release build, 84/84 CTest,
doctor and development package refresh pass via `tools/auto_verify.ps1`; no game
or headset run occurred.

Ghidra 12.1.4 now runs with the pinned JDK 21 via `JAVA_HOME`,
`GHIDRA_INSTALL_DIR`, and a 32 GiB headless heap. Read-only `DSRFull` queries
work through `tools/ghidra/query.ps1` with read-only decompile/caller scripts; `0x1402472D0 → 0x1400BE790`
reconfirmed command `0x3B` writes four perspective floats, while
`0x1411C7680` composes camera `+0x60/+0xA0/+0x20` into draw-context blocks.
This remains static evidence, not permission for eye replay or camera writes.

Read-only Ghidra work in the separate `analysis-full-attack/DSRFull` project
recovered an AI attack-request/candidate queue, not a proven player-contact or
damage path: `0x14060f610/0x14060f900/0x140610480 → 0x1405b3e30 →
0x1405c7760 → 0x1405dfe90 → 0x1405dfb70 → 0x1405ea120`. The attack entry takes
six arguments and packages 28 bytes; the queue resolver takes seven arguments
and reaches candidate/hint state. `aiCommon.luabnd.dcx` contains compiled Lua 5.1
`continue_attack.lua` (2,292 bytes, SHA-256
`e58bcffe0c392af051e405b826fd90fd192db842e6e7097c627fdbc1bef6a763`), extracted
and decompiled outside the repo. It calls `SetAttackRequest(behaviorNo)` and
polls `IsHitAttack()`; native descriptors map these to `0x1405b3e30` and
`0x1405b58b0`. The predicate reads AI state `+0x1f4`; state loader
`0x14038dae0` supplies that byte from field `+0x1dc` bit 2. This confirms AI
hit-state plumbing only.

`FUN_1405d1ba0` has an 18-argument ABI (owner pointer, event dword, 16 floats),
confirmed by callsite `0x1405c3e40`; it creates/enqueues an event record. Its
adapter family is registered as `AddTopGoal` (`0x14002c940`), so this is AI
event plumbing, not a damage sink. `GetDamageLastFrame` (`0x14002c490` →
`0x1405b3c10`) only reports whether an AI damage-history record exists via
`0x1405d28b0`. Event type handling flows through
`0x1405c4ac0 → 0x1405d1470 → 0x140629330`, then queued handlers dispatch through
`0x1405d16d0`. Scanned 39 script DCX/BND3 archives (837 entries) and 66 event
DCX archives; no literal `CreateDamage_NoCollision` or
`OnCharacterHP_CheckAttacker` calls appeared. Lua assets contain the AI methods,
while those damage names remain native registrations only. Next resolve their
numeric EMEVD/handler mapping or trace a registration to a concrete HP write.
Primary-project raw instructions now confirm generic `0x1404e8350` consumes
nine arguments (queue, event dword, payload, byte, three dwords, two bytes).
Its 16 direct callers cover event wrappers including `OnEvent_Bonfire`; the
body allocates a `0x48` record, initializes it via `0x1404e7e80`, links it via
`0x1404e8aa0`, and returns queue-insertion status. Caller `0x1404ad25e`
supplies the ninth argument; the 12-argument pseudocode is not corroborated
by callee reads. This is event plumbing, not a damage sink. REA MCP is enabled,
but Ghidra provider remains unavailable on Windows until its process ownership,
private-DACL and reparse-safe launch gate is satisfied. Global server config now
supplies `GHIDRA_INSTALL_DIR` and JDK paths; restart pending. Do not bypass the
gate; read-only headless Ghidra remains the working query path. No physical
weapon write/replay.

`CreateDamage_NoCollision` registration `0x1400200c0` maps through descriptor
`0x141a2fd10` and thunk `0x1404b0ec0` to `FUN_140490f40`. Raw instructions
confirm seven argument positions: manager context, dword, two actor handles,
and three stack floats. The wrapper resolves both handles and calls
`FUN_1403c9b50` with a vector; that allocates an event-record ID. The observed
consumer path `FUN_1403c8dd0 → FUN_1403c6030 → FUN_1403c6850 → FUN_1403c7520 →
FUN_1403c4020` performs contact/overlap work and records outcomes. The
`OnCharacterHP_CheckAttacker` descriptor also queues event type `0x0d` through
the generic queue builder. No direct HP write is proven on either trace; API
argument meanings, event mapping and HP outcome remain open, so do not call it
from DSVR yet.

## Torso finish + roomscale attacks (offline) — 19 October 2026

Head-only torso commit fixed: `ArmIkTick` discarded packets with both
controllers invalid, and the pose commit required a solved arm, so losing
both controllers froze the torso while the head kept moving (live-visible
defect behind `arm_pose_native:420`). Head-basis packets now publish and
commit their torso correction alone; commit re-verification proves only
torso/lean bones differ. Full gate **82/82**, doctor PASS, package
refreshed (`tools/auto_verify.ps1`).

Physical roomscale attacks, offline slice (no native damage ABI traced;
no HP edits or canned swings substituted):
- `BladeKinematics::FeedRelative` measures velocities relative to the
  head: physical walking (head+hands translate together) no longer reads
  as swing energy, walking into a target fires nothing and does not
  consume the rehit gate, and a genuine swing on top of a walk keeps only
  its own speed. Epoch change/head teleport/non-finite head reprime or
  invalidate instead of spiking. `input.cpp` feeds it the same packet's
  head position and tracking epoch. Turning in place still swings the
  tip (rotation is a real swing); rotational swept-volume coverage and
  the native blade axis/length remain untraced.
- `BladeFromCommittedPose` derives the blade segment from the committed
  native weapon bone (root * weapon origin) with the tracked blade
  direction carried by the root basis; per-weapon length stays a caller
  assumption until traced. Classification/haptics remain log-only until
  a native target source exists.

Headset qualification attempted (receipt
`build/steamvr-roomscale-20261019-run.json`, Restored; game-rewritten
`DSVR.toml` preserved as vault `test-DSVR.toml`): user could not
calibrate and could not test further — gameplay fell to ~16 FPS and
looking produced a spinning sensation. Log diagnosis: menus ran
79-90 FPS; `camera and targets` costs a constant ~8.3-10.9 ms whenever
the camera hook is active (sub-timers `camera apply (writes)` /
`arm target build` now split it, awaiting a run); runtime rate
oscillated 90->18 Hz under load. Calibration gates never reached
pose_ready (arms-out hold unachievable at that rate/spin); B correctly
did not cancel (on-screen Cancel is the exit — user instruction fix,
not code). Spin: turn-in-place torso follow applied a fixed per-frame
step, frame-rate dependent; now rate-limited via `TorsoFollowDeltaDt`
(1.5 rad/s) with tests. Perf candidate `build/perf-candidate.toml`:
desktop_present=false (new config: skip desktop swapchain Present in
VR; native limiter still paces), render_scale 85, snap turn 45
(smooth off). Stereo direction per Ghidra corpus
(`C:\Tools\DarkSoulsRemaster_De`, read-only): the engine emits
per-view command streams (`EmitFrpgRenderViewCommands`, six-view
cubemap in `BuildFrpgSixViewPasses`) executed on workers, so true
stereo needs an execution-side bracket still unproven; the 90-120 FPS
stereo route is depth/rotation reprojection in the existing blit
(depth capture via D3D11 context vtable at device capture), with true
stereo only after mono is fast. REA deep analysis remains unavailable
(provider down); corpus JSONL used directly.

Perf run 2 (receipt `build/steamvr-perf-20261019-run.json`, Restored):
desktop Present 0.000 ms (new `desktop_present=false` works), menus
88-90 FPS, camera+targets 8.3 -> ~4.1 ms. Sub-stage split at ~53 FPS
gameplay: camera apply 3.2 ms, arm equipment 1.7 ms, arm target build
0.7 ms, validation 1.1 ms, commit 0.9 ms, solve 0.06 ms — apply and
equipment are the next cuts. User report: game runs much better on the
saved profile; body lean works. Two fixes shipped from this run
(82/82, package refreshed): (1) calibration pose gates are now named
live on screen (`pose_hint`: wider/level/raise/lower/beside-shoulders)
and fully logged (both hand positions + hint), since the hold failed
silently on an invisible bound while trigger/span were fine; menu nav
during calibration remains stick/A/B with the on-screen Cancel button.
(2) Camera rides the solved head: the pose commit publishes the solved
head bone's world displacement (`ArmIkHeadWorldDelta`, same freshness
contract as hands), and the camera adds it damped to its anchor — the
viewpoint now moves with torso lean/turn instead of watching the body
lean away from a fixed root point. This is the E5 eye-to-head coupling
with native animated head pivot as the reference (no authored eye
offset exists); exact eye placement inside the head still needs
in-headset judgment. Stereo reprojection is the next build; a
diagnostic run for the apply/equipment split is still owed.

## Input-edge continuation — 19 October 2026

`FeedInput` sampled keyboard/mouse state per render frame, so a full press/release
between polls disappeared. `MenuWindowProc` queues bounded key and mouse-button
edges for render-thread delivery. Close, calibration-cancel and focus-loss paths clear
external input and schedule a render-thread reset of ImGui's deferred event queue and
key/mouse state before the next
`NewFrame`. The actual `mod_menu_render` regression injects a queued click, closes and
reopens the menu, and verifies no click survives; it failed before this fix and passes
now. Full CTest is **81/82** because unrelated `test_body_pose.cpp:329` fails on low
hands still influencing torso yaw. DSVR Doctor passes; game remains stopped and no HMD
qualification was attempted. N3.1–3 offline input coverage complete; N3.4–6 remain open. Native inventory
  auto-detection and physical headset comfort/readability still need validation.

## Torso-follow continuation — 18 October 2026

Resumed from the last SteamVR feedback: menu navigation passes, calibration
passes, forearm rotation is better, but arms look uneven and the player body
does not follow head movement, so arms tangle/limit at range; conclusive IK
judgment waits on camera/body work. This slice implements the documented
E5 direction offline: the pose commit now yaw-retargets the validated
Spine -> Chest -> Neck subtree toward the body heading plus the head's
clamped yaw offset (twist bound 0.8 rad, per-frame correction bound
1.2 rad) before either arm solves, so shoulders share the wrist targets'
frame. The spine chain is derived structurally from the Neck parent chain;
rigs without it keep the previous behavior exactly. The camera now also
publishes the written head basis (`CameraHeadBasis`) beside the heading
frame. IK reach scaling clamps per-side deviation from mean reach to
3 cm (`CalibrationReachForScale`); the stored profile keeps raw asymmetric
measurements. New coverage: `test_body_pose` (40 checks including reflected
roots, twist clamp and atomic refusal), a native torso scenario, and reach
guard cases. Full gate: **82/82 CTest PASS**, doctor PASS, development
package refreshed by `tools/auto_verify.ps1`. Not headset-qualified:
tangle/uneven feel, overhead/lowering and wrist review still need a
SteamVR run; E5 remains open (lean double-add removal, pelvis/foot
turning, native animation transitions, eye-to-head offset still undone).
No game launch, install or save change in this slice.

Follow-up the same evening: lean now rides the torso (`SolveTorsoLean`,
0.30 m clamp, calibrated profiles only) and the grip mapping's separate
lean addition is removed; a native check proves the torso correction
reverses cleanly between frames instead of accumulating. A protected
SteamVR run (receipt `build/steamvr-torso-20261018-run.json`, Restored,
doctor re-verified, saves intact) attempted headset qualification at
21:20-21:24 EDT but obtained none: the mod logged repeated
`xrCreateInstance failed: result=-2` because the SteamVR compositor and
headset link were not up when the game started, and the game process
was gone by about 21:23 before any XR session existed (user closed it
or it exited; unconfirmed). Torso/lean/reach behavior therefore remained
offline-only evidence at that point.

Second run (receipt `build/steamvr-torso2-20261018-run.json`, Restored;
DSVR.toml runtime rewrite reviewed and preserved as vault
`test-DSVR.toml` before restore) with the compositor verified up first:
XR established at 90 Hz. User reports body follows and arms look even,
and overhead/lowering plus wrist rotation feel usable. Two defects:
the camera ended up inside the torso (log shows the torso yaw
correction saturated at 1.200 rad while pitched 46.6 degrees down), and
the calibration hold did not react to the trigger, so setup was
skipped on the saved profile (runtime config also flipped
`play_style` seated -> roomscale during the run; link unproven).
Fixes shipped and gated (82/82 + doctor + package): the torso
correction bound drops from 1.2 to 0.8 rad so locomotion facing can no
longer wind the chest across the camera, with a regression test; and
calibration now logs its capture gates once per second while setup is
requested, so a non-starting hold is diagnosable from the log.
Third run (receipt `build/steamvr-torso3-20261018-run.json`, Restored
after the same reviewed `test-DSVR.toml` preservation) re-qualified the
fixes: user reports the camera stays clear of the torso, arms follow
and look even, overhead/lowering and wrists remain usable, and the
calibration hold now registers and completes with the release-then-hold
sequence. Log shows torso corrections bounded at +/-0.800 rad and
gameplay around 30-44 FPS (90 Hz target still unmet; known open
performance item). Remaining calibration defect, diagnosed from the
same log plus code: pressing B on the calibration screen called
`CalibrationCancel()`, silently killing setup so triggers did nothing
until recalibration was reopened. B no longer cancels calibration
(explicit on-screen Cancel button only); this one-line UX fix is gated
offline (82/82 + doctor + package) and awaits a quick headset confirm.
Sampling also starves while the XR session is unfocused, by design.

Solver pass implemented per the approved spec
(`docs/superpowers/specs/2026-10-18-torso-ik-solver-design.md`): torso
yaw is now distributed Spine/Chest/Neck (45:35:20, caps 0.45/0.35/0.50
rad, one redistribution), the pelvis takes a capped half share
(+/-0.40 rad), the target is the middle ground between head yaw and
hand pull, lean pitch distributes Spine:Chest 60:40 past a 0.15 rad
deadband, and the head bone snaps to the camera facing/up through the
neck (0.60 rad cap). Torso state is damped at 90 ms with deadbands so
resting micro-jitter cannot vibrate the body; resets on rig rebind,
root jumps, calibration resets and menu/calibration capture. Pure
coverage reaches 100 checks in `test_body_pose`; the native fixture
proves head snap, pelvis share, grips on target and reversal without
accumulation. Full gate green (82/82, doctor, package). Headset
qualification of bend feel, stillness and the B-button confirm is the
remaining step.

## No-HMD session — 18 October 2026

Final source integration now passes **81/81 tests**, doctor and development
packaging (`build/no-hmd-final-auto-verify.txt`). Full build exposed a missing
namespace qualifier in desktop readiness, a missing-recovery-record uninstall
case and incorrect calibration test bounds; all are repaired. Fresh preflight
now confirms **13/13** original hashes (`build/no-hmd-current-preflight.json`),
superseding the earlier missing-root observation below without restoring or
modifying saves. SteamVR follow-up reached gameplay and saved calibration.
Runtime: SteamVR/OpenXR Meta compatibility 2.18.4, 90 Hz requested, recommended
eye size 2916x3120, production mono. Gameplay intervals were about 32–35 FPS;
camera/targets ~6.6–6.8 ms, native camera/restore ~2.3 ms, arm retarget ~2.8–3 ms,
solve ~0.02 ms and commit <1 ms per measured call. Native cap setter ran once.
This is a new runtime baseline, not a controlled VDXR comparison or 72/90-Hz pass.
User feedback: calibration provisionally passes and helmet is hidden; native
menu navigation fails, wrists pinch with missing arm rotation, and two surfaces
remain at eye level facing forward. Overhead/lowered motion was not qualified.
Follow-up SteamVR feedback (same run series): menu navigation now works and
calibration still passes; forearm rotation feels much better but wrists still
need review. Arms look uneven and the player body does not follow head
movement, so arms tangle/limit at range. User states conclusive IK immersion
judgment needs the camera work first. Eye-surface removal and overhead/lowered
motion remain unconfirmed in-headset.
The follow-up source fixes keep startup theater/menu navigation active until
the first natural gameplay camera sample, distribute controller pronation to
forearm/twist skin, and exclude the separately generated FaceGen/eye layer
after verifying its native owner/resource chain. Focused regressions were red
before each fix and now pass; the candidate still needs headset qualification.
Native evidence and the slot-0 classification correction are recorded in
`build/native-mesh-filter-findings.md` (no extracted assets/decompiled bodies).
Receipt `build/steamvr-20261018-run.json` is Restored; all 13 originals match
`build/steamvr-20261018-restoration-check.json`. Log/metadata are retained in
`build/steamvr-20261018-native.log` and `build/steamvr-20261018-metadata.json`.

Wrap-up implementation: execute-only reads are refused, and SEH read faults
invalidate active/suspended permission caches; nested/thread/capacity/cross-region
coverage passes. CPU logs now drain bounded intervals while UI retains resettable
totals; generation checks reject samples spanning a reset. Calibration preserves
asymmetric lateral reach estimates, uses double accumulation, resets interrupted
holds and clears stopped input capture. Desktop-only Present readiness grants
blur enforcement without XR/gameplay activation; partial-hook rollback and durable
reinstall/temp-file recovery have focused coverage. Final integration receipt:
`build/no-hmd-final-auto-verify.txt`. Desktop runtime/blur rendering and HMD
acceptance remain open; short input-edge handling is unfinished. See the updated
[no-HMD checklist](docs/superpowers/plans/2026-10-18-no-hmd-session-plan.md).

Source baseline and previous DLL/package retained in `build/no-hmd-baseline`;
source and binary hash manifests retained beside it. The earlier N0 preflight
matched 8/13 files and observed an absent alternate root; that observation is
superseded by the fresh 13/13 checks and restored test receipt above. No save data
was reconstructed to resolve it. Desktop VR-disabled rendered-blur acceptance
remains open; this headset run used VR enabled.

## Previous checkpoint — 18 October 2026

Release build, **77/77 CTest PASS**, doctor and latest development packaging pass
(`build/e9-latest-auto-verify.txt`, `build/latest-ctest.txt`). Latest package:
`dist/DSVR-latest-x64.zip`; this is an incomplete development preview, not a
formal release. The inactive frame pump is integrated: real mono-entry
regression reproduced blocking before the fix; fixtures now cover exclusive
handoff, deferred STOPPING, same-handle restart and fault recovery.
Protected inactive-only startup (`build/e1-startup-run.json`) reached title at
55–60 FPS with VDXR reporting no available system. Home rendered; all 13 original
save/install hashes were restored (`build/e1-startup-restoration-check.json`).
This is desktop fallback evidence, not a connected headset or gameplay FPS pass.
The subsequent connected run (`build/e1-connected-native.log`) reached calibration
near 90 FPS but calibrated gameplay reproduced 13–15 FPS and user-reported
first-person/IK/third-person flashing. Controller A/B and movement work; native
menu navigation does not. Original 13 files were restored and verified.
Nested arm probes (`build/e2-profile-native.log`) isolate ~9.7 ms equipment,
~14.3 ms commit and ~0.02 ms solving; validation includes an expensive first rig
discovery. That run is also restored/hash-verified. Scoped read-permission reuse
now avoids repeated VirtualQuery within one callback; no data is cached and each
copy keeps SEH. Actual query-count regression was red before reuse and passes
afterward (`build/e2-memory-query-before.txt`, `build/e2-memory-ready-ctest.txt`,
77/77). Camera apply/restore and arm snapshot/commit use short independent scopes.
Native performance receipt `build/e2-memory-run.json` is restored and all 13
hashes verified (`build/e2-memory-restoration-check.json`); log retained in
`build/e2-memory-native.log`. User confirmed flashing stopped. Gameplay samples
improved to roughly 33–40 FPS in the initial stationary comparison; later values
varied with menus/scene and do not establish a sustained target rate. Commit
dropped below 1 ms and solve remained ~0.02 ms. No resolution/quality downgrade.
Next fix refuses non-player pose callbacks before equipment reads. The actual
adapter test was red (`build/e2-npc-before.txt`) and is now green; integrated
77/77 PASS (`build/e2-npc-ctest.txt`). Native receipt `build/e2-npc-run.json` was
desktop-only: VDXR reported no available system while the headset charged.
The run is closed; all 13 originals match `build/e2-npc-restoration-check.json`.
No native FPS gain is claimed for the NPC fix yet.

Controller UX: theater/front-end native menus now receive left-stick D-pad
directions; immersive locomotion remains analogue. Home accepts A or trigger;
calibration receives stick/A/B navigation, keeping its hold trigger reserved.
Actual input regression was red for missing D-pad/calibration routing
(`build/e9-input-before.txt`) and now passes. In-game inventory/menu automatic
context detection remains unbound; this does not qualify all native menus.
Calibration play-position selector width is constrained to the instruction panel;
WARP preview visually verified (`build/e9-calibration-preview.bmp`). Headset
acceptance of the new controls is pending. No game or transaction remains active.
Calibration preview was visually checked at
1280x720 (`build/calibration-current.png`). True stereo/AFR and native physical
melee/block/parry remain unbound; full-body retargeting is unfinished.

Latest user policy: run calibration once each launch, then retain measurements
through all deaths/loads until explicit recalibration. A saved profile remains
available for cancellation/recovery. Physical tracking origin and stick heading
now also survive native camera suspension, Home/theater and respawns; only explicit
recenter/calibration or a new XR reference space replaces them. Tests reproduced
crouched-respawn origin drift and suspension heading loss before fixing them.
Seated floor-to-eye height no longer shrinks estimated shoulder width/reach:
identical arm spans produce identical anatomy estimates while measured eye height
remains separate. The calibration screen has an explicit play-position selector.

The last restored Quest 3 run (`build/upgrade-hmd-run3.json`) confirmed improved
forearm/wrist feel but failed visibility (pelvis-to-elbow stretching, intermittent
body/camera obstruction) at about 15–16 gameplay FPS. IK off alone remained slow;
headlook off measured about 45–46 FPS. Its 13 original save/install hashes were
restored (`build/upgrade-hmd3-restoration-check.json`).

All bone-collapse visibility code has now been removed. New `body_visibility`
hooks native modular update213900 and part-queue writer219230. A guarded local
player binding filters only native face/headgear/hair slots1/2/6 in a 13-record
scratch copy; native part dirty bit18 requests restoration when policy/theater
changes. The original parts/palettes remain intact. Exact float ABI/prologues,
queue mapping, stock empty base mesh, and armor coverage are recorded in
`build/native-mesh-filter-findings.md`. Forearm-only remains unavailable: 58/222
armor-arm assets include upper-arm/clavicle skin weights and need finer filtering.

Quality-preserving performance fixes: XR blits now restore native DX11 state
(including predication/UAV counters); runtime submission cadence below60 Hz keeps
the last supported native cap instead of repeatedly resetting limiter history;
valid head/hand tracking no longer disappears because an XR image wait fails.
Actual XR/WARP call-site regressions went red before the changes. CPU stages and
submission-health counters are logged and available in Home diagnostics. Native
FPS improvement is not yet established. Notes: `build/fps-diagnosis-20261017.md`.

Protected candidate run `build/geometry-pacing-run.json` is **Restored**.
The user closed it because initial loading appeared frozen. The inactive XR
session remained synchronized but invisible: Wait/End calls blocked about
100–200 ms total, limiting native startup to about6.4 FPS. This is not a crash or
gameplay performance acceptance. Evidence: `build/geometry-pacing-inactive-startup.log`.
All13 original save/install hashes match (`build/geometry-pacing-restoration-check.json`);
original log restored and exact-game offline firewall block retained.

The user then requested planning before further implementation. The complete
remaining sequence and exit checks are in `docs/ENGINEERING.md`, Remaining work
plan. Task-level execution is in
`docs/superpowers/plans/2026-10-17-dsvr-execution-plan.md`; it is planning-only and
does not mark unfinished native work complete. A draft `xr_inactive_frame_pump.h` and test exist from the interrupted work;
they are not integrated into XR submission and not included in the76-test result.
No game is running and no protected transaction remains active.

## Previous checkpoint — 16 October 2026

**Dist + pacing + stereo bracket (source, this session).** Spec:
`docs/superpowers/specs/2026-10-16-dsvr-dist-pacing-stereo-design.md`.
(1) Every green `tools/auto_verify.ps1` run now refreshes the overwritable
dev test package `dist/DSVR-latest` (+ verification JSON with binary hashes
and CTest totals, + CTest log); failures keep the previous package. Formal
immutable archives unchanged. (2) `display_rate.h` makes the live OpenXR
period the pacing authority (quantized, hysteresis-stabilized, config is
fallback): `RateMatch` on change, `RateEvidence` (measured game FPS) every
~5 s. Native limiter patch still pending — requested is never reported as
achieved. (3) `stereo_render.h` implements the native bracket contract
(single token/prediction, distinct eye targets, mask-3 submit, whole-pair
cancel, per-eye `NativeEyeClip` pairs) behind a draw-callback seam. No
native hook installs it yet: the proven main-scene draw span is still the
gate, so live output remains mono. Release build, **55/55 CTest PASS**
(+`display_rate`, +`stereo_render`, packager 8 tests), doctor PASS;
`dist/DSVR-latest` refreshed from that green run. Next: prove the draw
span, wire the bracket, then qualify limiter + stereo in a protected
headset run recording actual FPS + XR period.
Offline bracket check (same session): retained frame-2774 draws confirm no
clean contiguous main-scene span — 317 main-color (3402/3371) draws span
events 2579..9265 interleaved with 188 depth-only and auxiliary-target
draws. Bracket selection must key on camera + targets + pass, and its
lifetime/replay safety still needs the bounded protected native
experiment; guessing the span from the capture alone remains forbidden.

**Finished follow-on — XR image recovery; 90 FPS preference recorded.**
Actual mono submission now retains acquired images after wait errors/timeouts,
uses bounded waits and refuses submission after release failure. Cleanup retries
preserve failed owners/resources, wait for a teardown-only D3D11 completion marker,
and run before image drains at all three public XR entry points. Session stop
retains live image leases until release or actual owner destruction. A review
caught a pending-cleanup reentry gap; its public-entry regression failed before
the fix and now passes. Both review axes have no remaining findings.
New fake-runtime/headless-WARP fixture exercises actual `xr.cpp`; Release build
and **53/53 CTest PASS**, plus 10 repetitions each of both XR tests. Evidence:
`build/empryo-review/mono-*` and `final-source-ctest.txt`.

The user chose source work and deferred the headset test. No desktop window or
deployment is active. New factory configs and current developer candidates now
request **90 FPS**, and candidate deployment requires that setting. Future
native tests target game 90 FPS/OpenXR 90 Hz and must record both actual rates.
The native cap is still unapplied; runtime rate selection is separate. Static
inspection confirms the wait helper's second argument is an absolute frame
index, not FPS; do not replace it with 90. Native timing/history needs its own
verified rate-setting route. No newer archive or game-install change was made.

**Codex takeover — Empryo reviewed; input recovery finished (source only).**
Recovered exact pre-edit snapshots from the local Empryo session because this
workspace has no Git history. Standards review: no findings. Spec review: one
P2 gap in the unfinished turn-error patch; a failed first right-stick query
could be hidden by its successful duplicate, retaining queued yaw/snap state.
The actual-input regression failed before the fix. Pad and turning now share
one right-stick query; snap/smooth error and recovery cases pass. Reviewed
inline-hook ownership/lifetime changes also retain their passing tests.
Release build and **52/52 CTest PASS**. Evidence/baselines/diffs:
`build/empryo-review/`. No new native acceptance is claimed.

Next concrete check: controller-only `build/input-candidate.toml`, with arm/head
pose writes disabled, through the reversible `DeployInputCandidate` lab mode.
Its guard refuses write-enabled configurations before any deployment. Latest
live result remains head PASS, arms FAIL, old pad bridge FAIL; true stereo and
native combat remain unbound. Current saves and loader/core/config match the
finish-run originals; live Steam-owned cache metadata is retained separately.

**Continuation — game-local XInput fallback (source, not live-qualified).**
Picked up the finish-run pad failure rather than retrying native trampoline
allocation indefinitely. The actual adapter now falls back to one validated
loaded-game import after persistent `MH_ERROR_MEMORY_ALLOC`; ordinal/name
resolution and expected-owner CAS replace no executable bytes. The pinned PE
hash and unaligned import layout were independently rechecked. Modules stay
pinned through process exit; uninstall disables overlay, restores only its own
slot and preserves protection/cleanup state on failure. New `xinput_iat`
regression failed before implementation; both XInput tests now pass, including
fault injection, an ownership race and a paused callback across uninstall.
Independent source review found no introduced defect; added its missing pin/
cache-line refusal cases. Release build and **48/48 CTest PASS**; both XInput
tests also passed 20 repetitions each (`build/xinput-iat-stress.txt`). Repaired
inherited fixture expectations: nonfinite menu values use current defaults,
tracking-rotation assertions explicitly disable the provisional 12-degree roll,
and separate wrist/weapon tests cover mirrored zero/default/negative roll across
three controller orientations. Production arm/config behavior was not changed.
Evidence is `build/xinput-iat-*`. No game deployment,
save changes, desktop input or new headset claim in this continuation.

**Finish patch (source, not live-qualified): XInput, head culling, combat decision.**
XInput install now serializes MinHook calls across adapters, logs statuses,
and recovers transient/already-created create failures (run 9 lost the bridge
once); `xinput_bridge` regression was red before, green after. Selective head
culling collapses only the Head bone (`hide_head`, default true) in the pose
adapter; whole-model hiding unchanged. `EvaluateBladeContact` turns tracked
blade motion into one attributed hit per target/window (rest/lost tracking
fail closed). Candidate: `build/finish-candidate.toml`. Release build and
**47/47 CTest PASS**. True stereo rendering (two native scene views) and
native combat damage still require their traced bindings from
docs/ENGINEERING.md B/D; no mono-copy or HP-poke substitute was shipped.

**Finish live run (13:45, restored 0 mismatches): head PASS, arms FAIL, pad FAIL.**
Head culling verified in headset: looking down is clear, body visible. Arms
still wrong (details pending); grip references calibrated (1652, recalibrated
4925), ~6,000 commits, no foreign visits. XInput create now fails persistently
with `MH_ERROR_MEMORY_ALLOC` at first frame (Presence hook beside it succeeds);
next pad route is an IAT fallback in the game module, not more MinHook retries.
Receipt `build/finish-run.json`, candidate `build/finish-candidate.toml`.

**Run 8: startup failure isolated before arm activation.** The reviewed arm
candidate launched under a child-only VDXR override, then crashed at
`0x180137D90` before a render host or arm hook was installed. Windows dump places
the startup thread at that unmapped execute address while DSVR's loader thread
was installing the visibility hook. This matches the retained early-startup
failure; it is not live arm-write evidence. No controller movement requested.
Receipt `build/native-arm-run8.json` is Restored; independent check confirms
**13/13 original hashes**, zero mismatches. Steam remains open, desktop released.
Logs/events/offline dump analysis: `build/native-arm-run8-startup.log`,
`build/native-arm-run8-events.json`, `build/native-arm-run8-dump.json`.

Focused startup candidate now defers camera/presence/input/trace installation
until an accepted frame from the selected host, after host installation returns.
Observer/disabled/refused/failed-host paths cannot acquire those adapters; early
and reentrant callbacks are covered. The real startup-order regression failed
before the change; Release build and **46/46 CTest PASS** in
`build/deferred-gameplay-before.txt`, `build/deferred-gameplay-build.txt`,
`build/deferred-gameplay-ctest.txt`. Native startup recovery remains to be checked.

**Finished small patch: viewer head pose.** Native and mono tracking now locate
VIEW relative to the rendering base at the same predicted display time. Head
heading no longer comes from the left eye's possibly canted orientation. Eye
poses/FOV remain unchanged for projection submission. Failed head location
clears/refuses tracking; native cancels before borrowing images, mono submits no
image. VIEW-space setup rolls back on failure and is cleaned on stop/loss.
Independent source review found no actionable defect. Release build and
**43/43 CTest PASS**, `build/head-pose-build.txt`, `build/head-pose-ctest.txt`;
file hashes in `build/head-pose-reviewed.json`. Not deployed/headset-qualified.

**Arm-position source patch ready for native qualification.** Run 7 measured
16 complete before/after-copy pairs at `0x140357A2E` / `0x140357A7F`, then 16
more during a short walk. All 80 names/parents match the installed skeleton;
primary model-space 48-byte poses stay unchanged while the body copy changes.
The driver now runs after natural animation `0x140320980`, solves actual arm
chains and descendants, and retains native grip rotation. It binds the camera's
tracking sequence/space and refuses stale samples, foreign actors/threads,
changed pose ownership/hierarchy and shutdown. Defaults remain disabled/log-only.
Independent review fixes include serialized shutdown, invalidating a failed
camera recapture, and rechecking ownership immediately before writing. Their
regressions failed before correction. Release build and **45/45 CTest PASS**:
`build/native-arm-position-build.txt`, `build/native-arm-position-ctest.txt`.
No live write, rendered attachment or headset alignment claim yet.

Run 7 later crashed at `0x180152D10` (execute AV), with DSVR absent. The observer
fixture passes, but that does not explain or fix the game crash. Further native
qualification uses the in-process adapter without debugger attachment. Quit/reload
smoke remains unqualified. Receipt `build/native-pose-run7.json` is Restored;
independent `build/native-pose-run7-restoration-check.json` confirms **13/13
original hashes**, zero mismatches. Steam stayed open; desktop control ended.
Evidence: `build/native-pose-run7-snapshot.json`, `build/native-pose-run7-walk.json`,
`build/native-pose-run7-modules.json`, crash metadata and scene screenshots.

### Retained checkpoint — 15 October 2026

**PC capture run 6 completed:** after normal Steam sign-in and renewed five-minute
permission, loaded the existing disposable character with the local DSVR loader
absent. Pre-attach module inventory confirms no DSVR module. Two bounded hardware-
breakpoint captures recorded 32 selector entries and 32 scene-collector entries
across three render workers. The latter contains 201 list entries / 16 distinct
entity pointers, no per-list truncation, with native draw callbacks
`0x1402161F0` and `0x140220000`. Both observers report register restoration and
detachment; the game stayed responsive through subsequent menu navigation.
No movement/combat input or headset test. Quit/reload smoke remains outstanding;
menu navigation alone is not that acceptance check.

Receipt `build/native-plan-run6.json` is **Restored**; independent
`build/native-plan-run6-restoration-check.json` verifies **13/13 original
save/install hashes**, zero mismatches. Steam PID 32096 remains open. Desktop
control has ended. Captures and limits: `build/native-plan-run6-summary.json`,
`build/native-plan-run6-snapshot.json`, `build/native-plan-run6-collectors.json`,
`build/native-plan-run6-models.json`, and scene/after screenshots.

Run 5 reached the same character and captured 32 entries, then crashed at
`0x180152D10` (execute AV; Windows dump retained). DSVR is absent from the dump's
module list; an older dump has a similar +0x40000000 address-offset pattern.
Do not attribute either to DSVR or debugger presence without further evidence.
The observer did have a separate reproducible bug: it consumed all software
breakpoints. `python -B build/check-observer-debug.py` reproduced a target handler
count of 0 (FAIL), then 1 with 32 samples and a surviving target (PASS) after
forwarding target breakpoints. This fixes the observer defect, not a proven game
crash cause. Event deadlines now also apply during continuous event delivery.
Run 5 originals were restored with **13/13 hashes matching**. Run 4 was blocked
by Steam sign-in, restored, and is superseded by this successful signed-in run.

**Current continuation — camera ownership fix:** removed all legacy container-as-
matrix accesses from `camera.cpp`, including writes beyond the proven 0x18-byte
CameraMan allocation, projection restoration and Present-time reads. No guessed
slot replacement was introduced. First-person edits remain on the observed
follow source/working matrix and its +0x50 projection. A fresh atomic title
heuristic comes from the natural follow callback; missing/older-than-250-ms
samples mean unknown. Removed the obsolete broad offset scan.
`test_camera_writes` exercises the real camera implementation with recording
memory seams: failed before the fix, passes after; projection edits/restoration,
newer native edits, coherent camera/IK frame and title expiry are covered. Full
Release build and **43/43 CTest PASS**, `build/camera-owner-fix-ctest.txt`.
This is fixture/source proof, not a new live camera or headset qualification.

User confirmed Quest 3 plus charged motion controllers, using Virtual Desktop
and Steam Link separately; prefer VDXR for debugging and SteamVR for immersion.
User asks the agent to collect technical proof and provide numbered instructions
only when human participation is needed. Two authorized PC capture windows have ended. No headset or additional human
technical investigation is needed for offline analysis of the retained evidence.

Exercised `build/native-plan-observe.py` at natural `0x1411C9740` selector calls;
`build/native-collector-observe.py` samples the observed `FrpgOTCollector`
callback `0x1401ED960`. These are local developer observers, not feature acceptance
or release artifacts. Static native RTTI maps
are in `build/draw-plan-vtables.json` and `build/draw-context-vtables.json`.
`TargetSurface` cleanup `0x1411B7710` invokes surface flush methods and clears
context handles; deferred-plan `0x1411C8090` finishes/replaces a renderer and
rotates a worker index. These cannot be blindly included in eye replay.
The temporary native runs are restored; no new feature deployment or release was made.

**Resumed from latest OpenCode:** `session_v2` session
`ses_ec8cdf17fffdT6RWZReyukItd5`, title “OpenXR IK arm controls for fluid combat”.
Latest user direction there: “wire it so this build is stereo”. The last work
added the XR eye-frame API (42 tests) then traced native rendering; it stopped
before connecting native draw replay. Preserve ponytail mode, existing-account
testing authorization and leaving Steam open between runs.
- Existing run-3 receipt is Restored; its saved verification reports 13/13
  original save/install hashes matching. This continuation only replayed the
  existing capture; no game launch, deployment or personal save/cache mutation.
- Sampled actual post-VS data for 21 main-color shader variants; 13 variants
  match their exposed positions through the uploaded matrix within `3.62e-6`.
  This includes rigid/skinned shaders, not every effect or pass. Evidence:
  `build/renderdoc-relative-camera.json`, `build/native-relative-camera-summary.json`.
- Added `stereo_math.h::NativeEyeClip`: explicit center-view eye origin/basis,
  asymmetric frusta, unchanged native depth, measured off-axis terms removed
  before rotation extraction, unsupported inputs refused with cleared output.
  Regression reproduces event-2579 GPU clip position and checks parallax,
  canted-eye edges and invalid inputs. Full Release build, **42/42 tests PASS**:
  `build/resumed-opencode-ctest.txt`. No live renderer calls this helper yet.
- Native `0x1411C9740` selects a camera, sets viewport/scissor state and iterates
  draw-mask callbacks; `0x14020DDA0` also changes material state and draw counters.
  `0x1411CD8A0` owns scratch/context construction, preparation and retirement.
  None is proven a safe entire-function two-eye replay boundary. Do not connect
  the XR API by blindly calling those methods twice.
- Next: establish a retained native draw plan/target bracket and XR/native pose
  mapping, then use the measured projection helper within that bracket and
  connect the existing XR begin/end API. Live output remains mono; no release.

**Latest — targeted GPU capture succeeded:** portable signed RenderDoc 1.46,
unmodded desktop run, `build/dsr-native-gpu_frame2774.rdc` (280,863,363 bytes).
Thumbnail shows Northern Undead Asylum; offline replay inspected **700 draws,
89 shaders, 243 textures**. Main color `ResourceId::3402` is 2560×1440
R11G11B10_FLOAT, depth `3371`; MRT secondary `3405`. Saved main color at event
9265 visibly contains the world/player without HUD. Late UI draws to `3448`
(9585–10474), final swap buffer `3451`. IDs are capture-local, never live offsets.
VS reflection identifies `gVC_WorldViewClipMtx` at slot 0 byte 0, camera position
at byte 64 and local-world bone palette at byte 448. Main draw 2579 callstack
passes `0x14020e5c4` and selector return `0x1411c984c`; shader instructions consume
the matrix for SV_Position. 238 of 316 slot-0 draws on main color share one matrix;
depth-only draws also use it, so matrix equality alone is not a pass selector.
Artifacts: `build/renderdoc-draws.jsonl`, `gpu-shader-*.json/.txt`,
`renderdoc-camera-summary.json`, `gpu-main-color-event9265-small.png`.

Run 3 receipt `build/gpu-frame-run3.json` is **Restored**. All 13 original save/
install files hash-verified in `build/gpu-run3-restoration-check.json`; test save
retained in `.dsvr-backups/desktop-observer-20261015-033813/test-root-0`.
Steam PID 19692 stayed running throughout; script no longer shuts it down or
overwrites its live cache. Cache snapshots retained, not claimed restored. Cached
Offline preference was false; exact-game outbound block held, and native server
login failed. No networking bypass, XR, new core or release deployed. RenderDoc
Python debugging setting temporarily disabled for automation, then restored.
Next: safe native per-eye bracket, current/previous camera-relative transforms and
auxiliary-pass exclusions, using this frame instead of another broad trace.

**Superseding runtime checkpoint:** two desktop transactions completed and restored;
no active save-root swap remains. Run 2 receipt `build/desktop-observer-run2.json`
is `Restored`; vault `.dsvr-backups/desktop-observer-20261015-031940` retains test
output. Original roots, loader/core/config and app Cloud cache restored; Steam
left stopped. First run also restored 14 originals. Offline client restart/account
selection reset its cached mode once; later preparation kept the running client.
Cloud toggle and shared stats effects were not isolated. Do not repeat setup work.

- Startup fault: execute AV `0x180137d90`, captured by bounded native debugger.
  Disabling pacing alone and all trace hooks still faulted; no native patches
  reached menu. F9-deferred activation then installed all observer hooks, stayed
  running into a loaded scene and completed two captures. Low-level cause still
  unresolved; legacy VR startup path has not been fixed or qualified.
- Observer startup no longer patches native functions. Capture activation is
  once-only, observer-restricted, refusal-safe and covered by tests. Release build
  and **41/41 CTest PASS**. Temporary debug bypasses/logging removed.
- Native evidence: `build/observer-menu_small.png`; captures
  `build/DSVR-trace-30248-234152765-1.jsonl` and `...234196406-2.jsonl`.
  Capture 2 wrote 1,294,376 events but dropped 2,483,135: broad upload tracing is
  unsuitable for complete call/timing associations. Offline analysis finds only
  ambiguous transposed matrix matches, not main-view/GPU consumption. Loaded
  character provenance is unproven; do not claim fresh-character smoke test.
- Next: targeted main-scene slot-0 constants/binding/draw/target evidence (RenderDoc
  if compatible), reduce hot-path trace load before another launch. No replay,
  stereo, tracked bone writes or physical-combat acceptance yet. No release.

### Earlier implementation history (superseded where noted above)

- Latest continuation: Steam Offline confirmed by user; headset available, but
  user requests continued autonomous progress rather than more setup questions.
  Cloud toggle remains unverified (do not claim disabled); no game launched here.
- Followed pinned static camera data through `HGDrawContext+0x158`, preparation
  `0x1411C7680`, shader callback/constant setup `0x140064A20`, upload
  `0x140CD0A10`, VS/PS binding and an indexed-draw caller. Located parser
  `0x1411C9740` camera-handle selection. Verified six function paths and upload
  prologue against installed EXE; `build/camera-gpu-static.json` records checks.
  This is a static path, not actual main-scene GPU consumption or replay proof.
- Corrected a provisional concern: derived `FrpgCameraEntity` vtable
  `0x1412F2908` inherits the existing hooked dispatcher; no per-instance type gate
  exists to fix. Added fixture coverage for derived identity instead.
- Added byte-gated natural constant-upload observation: exact four-argument native
  forwarding; source/context/buffer/count metadata, at most three complete 64-byte
  source blocks, capture-bound post-return event. No extra Map, GPU synchronization,
  XR or native-data writes. Upload regression first failed without the hook, then
  passed. Expanded tests cover zero/partial/huge vector counts, unreadable samples,
  capture replacement, inactive reads and signature refusal. Full Release build
  and **40/40 CTest PASS** (`build/constant-upload-ctest.txt`); failing-before
  upload regression in `build/constant-upload-before.txt`. No deployment, save
  swap or release. Current runtime validation is still pending.

- Latest steering: user abandoned failed Windows lab-account creation and
  explicitly authorized using their existing account. Do not repeat that setup
  request. Same-account preflight found game stopped and exact-game outbound block
  active; backed up and SHA256-verified 10 files from Documents and Roaming in
  `%USERPROFILE%\.dsvr-backups\personal-pre-observer-20261015-023938` (manifest
  included). Originals unchanged. Active-user saves differ across these roots;
  preserve both. Steam Offline/cloud behavior and a reversible disposable-save
  route remain unresolved; no game launch, save swap or deployment in this step.
  Shared Steam stats/achievement effects were disclosed, not claimed isolated.
- User now confirms Steam Offline mode (human-reported, not independently
  instrumented). Installed Steam `userdata/1843241039/570940/remotecache.vdf`
  contains a synchronized Documents save entry; app-local settings also record
  `last_sync_state=synchronized`. This is actual local Cloud history, not proof
  of current Cloud enablement. Check/disable the app Cloud toggle before any
  disposable-save swap; offline operation alone does not prevent later uploads.
  No save swap, launch or installed configuration change made in this preflight.

- User wants a real first-person VR product, full motion/physical combat/haptics/
  stereo, autonomous progress, and meaningful feature-rich test releases.
  Installation/launch permission is active; no repeated design approvals or old
  launch quota. Offline/save protection remain; publishing is not authorized.
- Replaced competing plans/handoffs/backlogs/reviews with five maintained docs.
  Removed the superseded planning directory and diagram; useful implementation,
  tests, game/reference libraries, logs and historical release archives retained.
  Research follows game-modding recon → safe lab → native vertical slice → oracle
  → broaden → package/showcase, not an approval/specification bureaucracy.
- Re-read implementation, local reference findings, key native corpus rows and
  upstream loader/OpenXR/ReShade sources. Corrected unsupported ME2 DSR claim,
  false bone/combat implications, “save guard” vs backup, stale install restrictions,
  “any runtime” and native-FPS claims. Added XR timeout/lifecycle and physical
  combat rate/attribution requirements. Save-system RE is a fallback, not a reason
  to delay trying a validated OS-owned disposable-profile route.
- Current source is mono: `XrSubmitMonoFrame` predicts after scene rendering and
  blits the same backbuffer to both eyes. Arm IK is log-only; native contact/damage,
  held body/attachments and automatic all-menu/HUD extraction are absent.
  Walking fusion/lag cause remains unproven; do not reintroduce player-yaw writes,
  output-derived orbit feedback, synthetic stereo acceptance or old canned swings.
- Previous consolidation checkpoint: Release **27/27 CTest PASS**; real package
  tests first exposed then verified doc/overwrite fixes. Auto-verifier defaults
  verify-only, lab OFF/add-on ON, explicit new-version packaging; old outputs refuse
  replacement. Existing tracking/heading/capture/runner/host/WARP tests retained.
- Implemented startup-only `[diagnostics] observer_only`: a separate desktop
  branch excludes render/XR, presence, pad and lab installers. Effective config
  disables gameplay settings without rewriting requested config; reload and repeat
  diagnostic startup cannot change mode. Follow hook returns before all legacy
  write/reset/IK/input paths. F6/F8 cannot invoke XR; F9 uses the bounded writer.
- Raw natural-owner sampler reads exactly the `0x18`-byte CameraMan container,
  both slot words/first words and committed follow geometry, never disputed
  container matrix offsets or a guessed active slot. Fixed off-thread snapshot's
  old container-as-matrix read. Existing legacy VR layout remains unresolved.
- Located CPU camera command acceptance via pinned RTTI/vtable/bytes:
  `HGCameraEntity` `0x14007B870` dispatches `0x3B/0x3E` to
  `0x1411BD890/1BD4C0`. Added read-only byte/vtable-gated observations of producer,
  payload/cursor/handle/result and post-original matrices. Preserve original calls,
  return values and native side effects; no replay, eye rendering or GPU proof.
  Command `0x3A` snapshots native camera history: another replay effect to preserve.
- Real startup regression failed before the observer branch, then passed. Targeted
  **11/11 PASS**, including real config reload/thread, actual follow/consumer
  detours with recording native seams, capture replacement, unreadable data and
  byte/vtable refusal. Full Release build and **37/37 CTest PASS**. Logs:
  `build/observer-final-ctest.txt`, `build/observer-native-static.json`,
  `build/observer-doctor.txt`; five-doc links/anchors valid.
- Protected native lab is still blocked: token unelevated and `Get-LocalUser
  DSVR-Lab` returns `UserNotFound`. No standard profile or Steam identity/isolation
  has been configured. Need one-time human OS/login setup, not another headset test.
  Shared field-note search rechecked; REA still reports missing configured Ghidra/
  JDK and unsupported Hopper host. Existing read-only corpus remains untouched.
- Installed core and both latest historical ZIP hashes rechecked unchanged.
  No deployment, game launch, personal-save/config mutation, release or upload.
- Read-only doctor now reports pinned EXE PASS, game stopped, old DSVR installed,
  **SteamVR** registered (runtime JSON found) and firewall block active. These
  supersede earlier historical VD/no-firewall snapshots, but do not prove Steam/
  game offline, protected save isolation or runtime behavior. Installed config
  still enables VR; do not assume source safe defaults describe that installation.

## Next execution, not another plan

Release source baseline rebuilt on 20 October 2026; `ctest --test-dir build -C Release --output-on-failure` passes 84/84. Bare `ctest --test-dir build` is invalid for this multi-config build (`Not Run`, missing `-C`), not a product test failure.

1. Stabilize mono pacing before headset acceptance. The latest protected run measured about 53 FPS; camera apply (3.2 ms) and arm equipment (1.7 ms) dominate measured CPU stages. Camera reads/writes already have separate timers; commit-time equipment revalidation now has its own `equipment read` timer, covered by a red-before/green-after `arm_pose_native` regression. Run a protected capture to collect these stages, then reduce measured costs and record actual game FPS plus XR period; a 90 FPS config is not evidence of 90 FPS.
2. Resume stereo from the retained draw-plan/target ownership and XR/native pose mapping. Prefer the documented depth/rotation reprojection path only after mono is fast. Worker-side per-view execution remains unproven; require left/right image correspondence, parallax/occlusion, and proof of no duplicate simulation, events, or Present before acceptance.
3. Keep combat log-only until the native blade axis/length, contact attribution, damage/stamina/poise ABI, and simulation-tick ownership are traced. Use the protected disposable-save transaction to trace actor/weapon → capsules → contact → damage; do not substitute canned swings or HP writes.
4. Re-run headset qualification with the compositor up before launch. Record actual rates; verify calibration, non-spinning stable view, body/arm/weapon/headgear visibility, controls, and comfort. Comfort still requires a human verdict.

Preserve the disposable-save transaction and verify restoration after every native run. No user-facing release until its feature gate passes; do not modify `C:\Tools\DarkSoulsRemaster_De`.

## Retained evidence / recovery

- Pre-cull docs: 33 files / 4,573 lines, ZIP CRC and every member SHA verified.
  Local recovery archive (outside active workspace):
  `C:\Users\Gabrielle Monlea\AppData\Local\Temp\opencode\DSVR-docs-before-consolidation-20261014-222804.zip`.
  SHA256 `3761bb0c2b542a3aef7627f96daf8b5c80c4b5baadee9b034097b29a226a2c38`.
  Historical text is recovery material, not current instructions.
- 0.16.0 ZIP SHA256 `3e9fb477ebb5b13ce17e0995b841cf35d60a7ae845572f4552d34410e719854b`;
  0.15.5 ZIP `33e6a3e3165f70ba654cd8c4bdce9d144fd1963bc9a8ce1c34deb24c11a365d5`.
  Installed core last checked `bc0ec5fde8687a4d97853c9c50cf8d8077c087d037902eb83b40b1d29cfaf93c`.
  These identify different artifacts, not source/runtime equivalence.
- Logs: `build/native-stereo-ctest.txt`, `build/native-stereo-stress.txt`,
  `build/positive-host-stress.txt`, `build/consolidation-ctest.txt`,
  `build/consolidation-auto-verify.txt`; `dist/*-verification.json` for archives.
  `DSVR-live-20261013.log` is append-mode historical data, not an intact current
  walking/stereo/skeleton capture. A teleport delay is not a walking-lag diagnosis.
- Native analysis corpus/reference paths and crucial addresses are in recon.
  REA deep analysis previously had stale Ghidra/JDK paths and unavailable provider
  support; no repair or successful deep-analysis result is claimed here.
