# DSVR testing and operation

[Master plan](../README.md) · Developer experiments ≠ user-facing test releases.
The user authorizes useful installation/launch/testing autonomously. No fixed
launch quota or repeated design approvals. Publication and human comfort are
separate; protect personal saves and don't seize active desktop input.

User preference for all future builds/tests: game cap and OpenXR target **90**.
Factory defaults and new developer profiles use `frame_limit_fps=90`. Verify
the runtime is configured for 90 Hz and record a predicted period near
11,111,111 ns plus measured game FPS. The native limiter hook is implemented but remains unqualified live; report any lower cadence explicitly.
Do not silently qualify a 60/72-Hz run as meeting this preference.

## Physical contact continuation — 20 October 2026

Release build, **84/84 CTest PASS**, doctor and development package refresh:
`build/weapon-interactions-auto-verify-final.txt`. The actual native-adapter regression
failed against the saved original (`build/weapon-interactions-before.txt`) and
passes with corrected world blade direction. Adapter coverage includes physical
walking rejection, real swing energy, duplicate callbacks, tracking loss,
recenter/menu/motion-disable and dominance changes. Pure combat tests cover
rejected teleports with a moving head and feed-mode changes; equipment fixtures
reject changed stance values/owners and actor types during snapshots.

These remain offline fixtures. No native hit, damage, shield block, parry,
stamina/poise or haptic outcome was exercised. The proving ground uses a .9m
assumed blade and synthetic target/guard contact; it does not qualify combat.

## No-HMD preflight — 18 October 2026

Latest source integration: **84/84 CTest PASS** (head-directed locomotion:
fresh native/head movement-basis handoff, epoch/menu refusal and
camera publisher invalidation), Release build and doctor PASS, development
package refreshed via `tools/auto_verify.ps1`. No game or headset run occurred
for this source-only slice. Prior torso-follow integration also passed doctor
and refreshed the development package via `tools/auto_verify.ps1`
(`build/latest-ctest.txt`). Earlier 81/81 integration, doctor and packaging
in `build/no-hmd-final-auto-verify.txt`. Fresh checks in
`build/no-hmd-current-preflight.json` now match all **13/13** originals and find
the alternate AppData root. This supersedes the earlier missing-root observation
below; no save restoration or mutation was used to resolve it. SteamVR is selected
for the user-authorized follow-up receipt `build/steamvr-20261018-run.json`.
That run is now Restored and all 13 originals hash-match
`build/steamvr-20261018-restoration-check.json`. SteamVR 2.18.4 (Meta compatibility)
at requested 90 Hz and recommended 2916x3120 eyes produced roughly 32–35 gameplay
FPS in mono. Interval CPU evidence is in `build/steamvr-20261018-native.log`.
User feedback provisionally accepts calibration and confirms helmet exclusion.
Navigation, wrist/arm rotation and remaining eye-level geometry fail; vertical
reach remains untested. Follow-up regressions reproduce startup menu selection,
hand-only pronation and the unfiltered generated-face slot. Source fixes pass
focused tests but await native qualification; this does not qualify full
embodiment, true stereo or target refresh performance.

`build/no-hmd-preflight.json` records no running game and no active transaction
receipts. Sources/tests/docs/tools and the previous development archive/DLL are
preserved in `build/no-hmd-baseline`; SHA-256 inventories are
`build/no-hmd-source-baseline.json` and `build/no-hmd-binary-baseline.json`.
The prior 77-test receipt is retained in that snapshot.

Fresh comparison against `build/e2-npc-run.json` matched **8/13** original files:
all five Documents save files and all three installed files match, but the five
inventoried AppData save files are absent because their root is absent. The old
13/13 restoration receipt is historical evidence, not proof of today's state.
`build/no-hmd-restoration-check.json` records the discrepancy. No save root was
created, restored or modified in this session. Do not deploy or launch a new
native test until the missing-root discrepancy is resolved; offline fixtures
and temporary-file tests do not depend on it. Steam-owned cache state remains
outside the restoration claim.

## Current candidate evidence — 17 October 2026

Release build, **77/77 CTest PASS**, doctor and development packaging:
`build/e9-latest-auto-verify.txt`, `build/latest-ctest.txt`.
Inactive-only protected startup (`build/e1-startup-run.json`) reached title at
55–60 FPS and Home rendered. VDXR reported no available system, so this verifies
desktop fallback only. All 13 original save/install hashes were restored in
`build/e1-startup-restoration-check.json`. Actual XR/WARP fixtures independently
cover blocked inactive Wait/End, nonblocking graphics handoff, deferred session
stop and same-handle restart; connected headset acceptance remains required.
Connected baseline (`build/e1-connected-native.log`) reached launch calibration,
then reproduced 13–15 FPS and user-reported first-person/third-person/IK flashing.
A/B and movement worked; native menu navigation failed. Nested arm counters in
`build/e2-profile-native.log` show ~0.02 ms solving but multi-millisecond guarded
equipment/validation/commit work. Both receipts are restored and all 13 hashes
verified. The scoped-permission-query candidate passes 77/77 offline tests in
`build/e2-memory-ready-ctest.txt`. User confirmed flashing stopped in that run;
initial gameplay intervals improved to about 33–40 FPS, with later variable
scene/menu samples. This is not a sustained 72/90-Hz result. The run is restored
and all 13 hashes match `build/e2-memory-restoration-check.json`.

The NPC-equipment-query fix passes an actual adapter regression that failed before
the change. Its protected native run (`build/e2-npc-run.json`) was desktop-only
because the headset was charging; no active performance comparison was possible.
All originals are restored/hash-verified in `build/e2-npc-restoration-check.json`.
Theater native-menu D-pad routing and calibration controller input are covered at
the actual input entry point (`build/e9-input-before.txt` records the regression).
A selects Home/calibration without leaking a native confirm, and the calibration
hold trigger is reserved. Rendered selector layout checked in
`build/e9-calibration-preview.bmp`; headset controls remain unqualified.
Real DX11/WARP previews, visually checked at 1280x720:
`build/menu-preview.png`, `build/calibration-preview.png`.

New coverage includes launch-only setup, physical-origin and stick-heading retention
through suspension/respawns, seated anatomy independent of floor-to-eye height,
actual native part-command filtering/restoration and rollback, XR blit state restoration,
tracking retention across image failures, throttled-runtime pacing stability, and CPU
stage timing publication. Regressions were observed before fixes in the corresponding
`*-before*` evidence files. Existing coverage includes calibration-worker save failures and persistence; real
pose callback and grip solver attach/move/release and wrist trims; lost-controller
tracking recovery; real native UI/blur callbacks; durable blur file recovery; logging;
and actual mono submission with permanently pending GPU queries. Memory fixtures,
fake XR and real WARP are used; these are not native game/headset passes.

Protected runtime acceptance must isolate/hash saves, preserve installed files,
retain offline firewall rules and Steam, record candidate hashes, then verify:
Home input capture, theater/UI restoration, blur suppression after graphics
settings changes, once-per-launch and explicit recalibration, repeated death/load/
recenter, overhead/lowered wrist alignment, forearm/armor visibility and grip
attach/release. Record actual game FPS, XR period, CPU submission and GPU timing
at 72 and 90 Hz. Restore and hash-check original saves and installed files.

True stereo/AFR and physical combat cannot be accepted on this candidate: their
production renderer/outcome bindings remain absent. Mono blits are not stereo;
gesture-triggered attacks are not physical contact combat.

Protected native smoke follow-up (`build/upgrade-smoke-run2.json`, Restored):
first startup crashed before the accepted frame with early Blur hook installation.
Blur hooks now defer with native adapters; two subsequent starts reached the
offline title screen. Actual Home overlay and diagnostics panel rendered; the
panel reported native UI visits and two forced-off Blur reads. VDXR returned
`XR_ERROR_FORM_FACTOR_UNAVAILABLE`, so this proves no headset/FPS result. A
no-session calibration prompt was found and fixed by requiring a running XR
session; all-UI load/gameplay detection still uses the existing camera heuristic.
Native two-hand, visual blur, UI hiding and alignment remain unqualified.
`build/upgrade-smoke-restoration-check.json` verifies all 10 inventoried save
files and all three installed-file hashes. Original log restored; Steam left alive.
Final source tests: `build/upgrade-smoke-fix-ctest.txt` (69/69).
Native screenshots: `build/upgrade-smoke-home-final_small.png` and
`build/upgrade-smoke-diagnostics2_small.png`.

Later protected Quest 3 runs did reach gameplay. The last restored run,
`build/upgrade-hmd-run3.json`, measured about 15–16 FPS and failed forearm/body
visibility; the user reported improved forearm/wrist feel but upper-arm stretching
and intermittent body/camera obstruction. `build/upgrade-hmd3-restoration-check.json`
records all 13 original save/install hashes restored. The collapsed-bone masking
has now been removed entirely. The new head filter excludes native modular face,
headgear and hair draw records; forearm-only filtering remains unavailable because
some armor meshes mix upper-arm and forearm weights. The new candidate requires
its own native run; offline success does not establish head/helmet coverage or FPS.

## 1. Automated development loop

Current takeover: Release build and **53/53 CTest PASS** in
`build/empryo-review/final-source-ctest.txt`. Standards review had no findings; the
single Spec finding was a partial turn-query recovery fix. The new actual-input
regression fails when the first right-stick query errors but a duplicate would
succeed. One sample now feeds pad/turning, clears queued snap/smooth yaw on error
and permits recovery. Red/green outputs are `input-first-query-before.txt` and
`input-after.txt` in the same evidence directory. Inline-hook tests additionally
cover foreign ownership, activation failure and paused calls across uninstall.

The follow-on `xr_mono_recovery` fixture runs actual mono submission against
fake XR and a real headless D3D11 WARP device. It covers wait error/timeout,
release error, retained acquisitions, suppressed layers/tracking, successful
recovery, partial owner-destruction failure, a GPU completion marker and cleanup
through Present/mono/native entry points. The frame-helper regression also
proves session stop cannot forget a held image. Red cases are
`mono-recovery-before.txt`, `mono-owner-stop-before.txt` and
`mono-pending-entry-before.txt`; final output is `mono-pending-entry-after.txt`.
Both XR tests passed 10 repetitions each in `mono-repeat.txt`. These are source
lifecycle checks; no runtime/headset recovery or true stereo claim follows.

Latest native result is `build/finish-run.json`: selective head hiding PASS,
arm alignment FAIL and old inline pad creation FAIL. It is Restored. The reviewed
fallback has not run in DSR. `build/input-candidate.toml` isolates that check by
disabling arm/head pose writes; `DeployInputCandidate` enforces those values
before copying any files. Live Steam cache metadata remains client-owned.

XInput IAT candidate: `test_xinput_iat` exercises the actual adapter with a
synthetic loaded PE and forced persistent MinHook allocation failure. The
regression failed before the fallback. Coverage includes ordinal/name imports,
measured four-mod-eight alignment, physical/motion overlay, another user slot,
paused/late calls across uninstall, foreign owners and install races, malformed
or duplicate imports, cache-line crossings, module-pin refusal and transient/
persistent page-protection failures with cleanup retry. It does not run DSR.
Initial IAT suite: **48/48 PASS**; 40 repeated XInput test executions also passed.
The native-arm fixture isolates tracking rotation with zero configured roll and
separately verifies both wrists/weapons at zero/default/negative roll and three
controller orientations, preserving grip positions and rigid weapon offsets.
Nonfinite menu-config tests now compare current defaults, not obsolete literals.
Evidence: `build/xinput-iat-before.txt`, `build/xinput-iat-build.txt`,
`build/xinput-iat-tests.txt`; full Release build/suite results go to
`build/xinput-iat-release-build.txt` and `build/xinput-iat-ctest.txt`.
Use `cmake --build build --config Release` and
`ctest --test-dir build -C Release --output-on-failure`; this workspace uses a
multi-configuration Visual Studio generator. Native polling and headset
acceptance still require a protected disposable-save run.

Run 8 arm candidate crashed at startup before arm activation or XR creation,
`0x180137D90`; offline dump and log retain the exact stage. Originals restored,
**13/13 hashes match**, `build/native-arm-run8-restoration-check.json`; Steam
stayed open. The next source candidate defers gameplay-hook installation to the
first accepted host frame after host installation returns. Real startup-order
regression failed before the change; **46/46 CTest PASS** afterwards,
`build/deferred-gameplay-ctest.txt`. Early/reentrant callbacks, failed host,
observer/disabled/refused paths are covered. These checks do not prove that the
native startup crash or live arm alignment is fixed.

16 October native arm-position patch: Release build and **45/45 CTest PASS**,
`build/native-arm-position-build.txt` / `build/native-arm-position-ctest.txt`.
The real adapter is tested with bounded native-memory/MinHook seams: link lengths,
descendants/twist bones, root preservation, tracking sequence/space, pose/actor/
thread ownership, same-address hierarchy replacement, commit-time owner changes
and concurrent install/shutdown. Failed camera recapture now clears the shared
frame. Stale-write, shutdown-race and camera-recapture regressions failed before
their fixes. This is source proof; live writes/rendered attachments remain pending.

Latest PC-only run 7 collected 16 animation/body-copy pairs at rest and 16 during
a short walk. All 80 bone names/parents match the installed asset; primary pose
bytes stay unchanged while assembly poses change across each sampled copy.
The game later crashed (execute AV `0x180152D10`) with DSVR absent. Do not treat
the corrected debugger fixture as a game-crash fix or repeat that capture route
for the arm qualification. Quit/reload smoke remains outstanding. Receipt
`build/native-pose-run7.json` is Restored, **13/13 original hashes match** in
`build/native-pose-run7-restoration-check.json`; Steam stayed open. Evidence:
snapshot/walk/module/crash JSON and scene screenshots under the same run prefix.
The next bounded candidate uses the in-process adapter, no debugger attachment,
and a process-only VDXR selection; leave the global registered runtime unchanged.

16 October viewer-head patch: Release build and **43/43 CTest PASS** in
`build/head-pose-build.txt` / `build/head-pose-ctest.txt`; independent review has
no actionable finding. The actual XR frame class is exercised with a fake
runtime whose eye orientations differ from VIEW pose, checks matching time/base,
preserves submitted eye poses, and refuses invalid/missing head location before
image acquisition. Mono uses the same tested head-location operation. These are
source/API tests, not new native stereo or headset acceptance.

Earlier PC-only run 6 completed after normal Steam sign-in. The disposable
character loaded offline, with DSVR absent from the pre-attach module inventory.
Two observers each recorded 32 natural entries, restored registers and detached;
the game remained responsive through later menu navigation. Collector samples
contain 201 entries / 16 distinct entity objects with no per-list truncation.
No scene replay, stereo, bone writes or physical combat was exercised. Quit/reload
smoke remains outstanding. Evidence: `build/native-plan-run6-summary.json`,
selector/collector JSON, module inventory and scene/after screenshots.
Receipt `build/native-plan-run6.json` is Restored; independent restoration check
confirms **13/13 original save/install hashes**, zero mismatches. Steam remains
open. Both five-minute desktop-use windows are over; no headset action is needed.

Run 5 crashed after capture (execute AV `0x180152D10`); Windows dump retained,
DSVR absent from its module list. Its originals were restored, 13/13 hashes match.
A local fixture found an observer bug separately: target software breakpoints
were swallowed. `python -B build/check-observer-debug.py` failed before the fix
(handler count 0) and passes after it (handler count 1, 32 samples, target alive).
Only the initial attach/requested stop breakpoint is consumed; other software
breakpoints are forwarded. Continuous debug events also honor the deadline.
The game crash's exact cause is unproven; do not call the fixture a crash fix.

Latest camera fix: full Release build and **43/43 CTest PASS**,
`build/camera-owner-fix-ctest.txt`. `camera_writes` failed against the old adapter
and passes after removal of out-of-container accesses. It checks the real camera
implementation, follow projection restore/conflict handling and title freshness;
native camera/graphics/stereo behavior remains unqualified.
Quest 3 and charged controllers are available. User runs Virtual Desktop/VDXR
for debugging and Steam Link/SteamVR separately for immersion. Qualify each
route separately; do not infer a connected headset from the registered runtime.
The latest observations were PC-only; no headset comfort or live XR qualification
follows from a registered runtime or successful desktop capture.

Latest continuation: full Release build succeeded and **42/42 CTest PASS**,
`build/resumed-opencode-ctest.txt`. Existing stereo-math coverage now includes
measured native shader rows/post-VS output, asymmetric/canted views, near/far
disparity and refusal of unsupported matrix layouts. Replay of the existing
frame required no new game launch or installed/save changes. These checks
validate projection calculations, not native stereo wiring or headset behavior.

Latest run 3: RenderDoc successfully captured/replayed an unmodded Asylum gameplay
frame, `build/dsr-native-gpu_frame2774.rdc`. Main-color export excludes HUD;
shader-bound constants and draw stacks are retained offline. Receipt
`build/gpu-frame-run3.json` restored both save roots and three installed files,
**13/13 hashes match**; test character save retained in its vault. Steam stayed
running (same PID). Future runs reuse it: no shutdown/relaunch ceremony, no live
cache overwrite. Original/test cache snapshots retained instead. Cached client
Offline preference is advisory; exact-game outbound block remains mandatory.
This run reached character creation and Asylum, but quit/reload smoke acceptance
is still outstanding. No live stereo or native skeleton-write claim follows.

Latest native run (15 October): temporary same-account observer deployment restored
using `build/desktop-observer-run2.json`; watcher retained all test output and
reported original roots/core/loader/config/app-cache restored. Steam left stopped.
Release build and 41/41 tests pass, including deferred activation and refusal.
`build/observer-menu_small.png` shows a loaded scene, not a proven newly created
disposable character. Two 20-second captures closed without I/O error but with
1,269,591 and 2,483,135 dropped events respectively. They are incomplete evidence,
not stereo acceptance or performance qualification. No headset test was requested.

1. Define the missing native contract or defect and its expected observation.
   Read sources/captures first; write a regression that fails on the defect.
2. Build and run offline math/ownership/WARP tests. Reuse existing tests; no
   fake adapters may certify the native boundary they replace.
3. Use a short protected native experiment only when offline evidence cannot
   answer the question. Name the build/config, scene, observables and timeout.
   A research run can be read-only; it need not be a feature-rich public release.
4. Capture logs and actual eye images/GPU state, inspect them, compare baseline
   and candidate, then change approach after three identical failures.
5. Bundle only validated player-facing progress into R1/R2/R3. No new diagnostic
   preview version merely because tests, logs or a hosting adapter were added.

Existing production lab is **OFF**. Runner tests cover deadlines, neutral release,
capture ownership and refusal; they do not establish a working native safe-scene
adapter. Prefer process-local bounded commands to desktop input, but don't enable
automatic movement from caller-supplied “ready” claims.

## 2. Protected native lab — practical first, not endless save RE

Known save/profile areas: Documents known folder
`NBGI\DARK SOULS REMASTERED\<id>\DRAKS0005.sl2`,
`%APPDATA%\DarkSoulsRemastered`, and Steam `userdata\<id>\570940`.
Inspect actual known-folder/provider destinations; Documents can be redirected.
Historical backups: `%USERPROFILE%\.universal-modder\backups` and
`%USERPROFILE%\.dsvr-backups`. Back up again before a new experiment.

Prefer a **standard, unelevated** Windows test user, initialized profile and local
interactive session, with OS-owned known folders and a new lab save. Run Steam,
DSR and automation as that same user/elevation; Steam may relaunch the library
EXE, so verify the actual game's user/session/path. No copied credentials or
owner's Documents/OneDrive mappings. This is a candidate route, **not implemented
isolation**. Verify permissions and XR availability; do not claim that changing
`USERPROFILE`/`TEMP` changes native known folders or Steam cloud state. Avoid a
native string/allocator interception unless the simpler route demonstrably fails.

Before movement or combat automation:

- Steam Offline + game Offline; verify outbound blocking for the exact EXE.
  Check whether this installed game exposes Cloud; disable it for the lab if
  present. DSR's retrieved store metadata does not advertise Cloud, which is not
  proof of installed behavior. Preserve personal client/cloud/cache data.
- Snapshot personal saves/config and test-owned install files. Protect original
  saves against the test identity; verify with OS permissions and actual final
  file destinations/handles. Cover create/write/truncate/rename/delete and directory
  rights, including cached handles. Review original ACLs as metadata; never test
  denial by attempting to mutate personal saves. Backups are recovery, not isolation.
- First run is a desktop-only **XR-disabled** create/save/quit/reload smoke test
  of a new disposable character, with narrowly filtered Process Monitor file and
  process identity capture. Changes stay in the lab; originals' hashes unchanged.
  Then do the bounded modded read-only camera/GPU experiment. If this cannot be
  established, stop and ask for the minimum necessary credential/setup help.
- Record a reversible deployment manifest (before/after hashes, created files,
  modified config/rules). Restore only test-owned changes after the process exits;
  never overwrite a newer unrelated file. Keep no mounted personal-save aliases.

Use one fixed known scene: stationary reference + walk/strafe/backstep + stop;
then sword/shield encounters and transitions. No arbitrary teleports or guessed
game-state writes. Prefer non-invasive tool observation over manufacturing a
giant native “ready” API. Each run has a deadline, neutral input release and
exact-PID termination path; stop on loss, transition, stuck input, hang or crash.

A Windows profile does not isolate Steam identity, shared `userdata`, achievements
or cached stats later uploaded. Strict account-state isolation needs a separately
entitled Steam account; otherwise disclose and resolve shared-account effects
before proceeding. Offline setup may need one-time login help. The XR registration
is machine-wide; running services do not prove compositor/input works in the lab
user's session. Check this after the desktop smoke test, not through failed
headset runs. Platform sources and caveats: [recon](../MODDING_PLAN.md#sources).

15 October check: the agent token is unelevated; querying `DSVR-Lab` returns
`UserNotFound`. Account/profile creation and any required Steam login need one-time
human OS/credential setup before this route can be validated. No game launch has
been substituted under the personal profile.

User subsequently chose the existing Windows account instead; do not keep asking
for lab-account creation. Same-account testing is authorized, not certified save
isolation. Before launch, establish a reversible disposable-save route covering
both known roots and Steam synchronization; backups alone do not satisfy that
gate. Shared Steam account-state effects remain disclosed and unisolated.
Preflight verified 10 save/config backups under
`%USERPROFILE%\.dsvr-backups\personal-pre-observer-20261015-023938` with a SHA256
manifest; originals unchanged. Documents and Roaming copies differ for the active
Steam user, so neither may be treated as a redundant copy or overwritten blindly.
Exact-game outbound blocking is active; Steam Offline state remains unverified.

Follow-up: user explicitly confirms Steam Offline. Local installed Cloud history
**is present**: active-user `570940/remotecache.vdf` tracks the Documents `.sl2`,
and the app's local config records `last_sync_state=synchronized`. This supersedes
store-only assumptions, but does not establish current Cloud enablement. Verify
the per-game Cloud toggle is off before swapping saves; restore originals before
ever returning Steam online. Same-account backup/swap is recovery, not an OS
security boundary; do not label it ACL/process-isolated.

User then said to continue without further setup requests; preserve that steering.
Cloud toggle status is still unverified, not silently recorded as disabled. Keep
all launches offline and preserve both save roots and Steam metadata before any
disposable-save experiment. Headset is available, but no stereo-ready candidate
has been demonstrated yet.

## 3. Evidence and acceptance

| Check | Required proof; absence stays UNVERIFIED/BLOCKED |
|---|---|
| True stereo | Same simulation state; runtime eye geometry; final consumed view/projection → actual left/right scene target → submitted image. Near/far parallax + occlusion inspected. Different textures/pixel hashes alone fail |
| Correct scheduling | No second-pass input/physics/animation/combat/event/history/retirement/Present advance; ordinary game speed matches baseline. Measure animations/stamina/projectiles as well as counters |
| Stable view/body | Fixed-head strafe/backpedal doesn't change view yaw; render-root/eye/grip residuals measured. Native arms/weapon/shield—not log-only IK—remain aligned during locomotion |
| Combat | Attributed native contacts/damage/cost/reaction; misses/rest cause no damage; rotational sweeps, multi-target, reentry, despawn/ID reuse, invulnerability, stamina exhaustion and tracking jumps covered |
| Rate independence | Identical recorded motion/contact at supported simulation/render rates: target ≤5% damage/stamina variance, no duplicate first-hit reactions. This is a qualification tolerance, not an achieved result |
| Lifecycle | Title/load/death/equipment/menu/focus/space/resize/session-loss transitions neutralize combat/turns and restore only owned state; no stale eye pairs or indefinite wait |
| Performance | Record CPU/GPU p50/p95/p99 eye-pair times, predicted period and missed/reprojected frames on named hardware/settings. Native 72 Hz means budget 13.89 ms for both eyes, 90 Hz 11.11 ms—not per eye. Default target p95 within budget with headroom; measured reprojection/60 Hz is explicitly a reduced mode, not native 90 Hz |
| Safety | Offline/profile/deployment preconditions demonstrated, personal-save hashes unchanged, no unexpected file mutations, bounded shutdown/capture completion |
| Comfort/feel | Human verdict on scale, depth, fusion, latency, clipping, hit feel and fatigue. Video, WARP and synthetic tracking cannot certify this |

Tolerances for geometric residuals and gameplay timing must be declared against
the calibrated native baseline **before** accepting a candidate. No arbitrary
“plausible-looking” tolerance chosen after seeing the result. Separate report
fields: integrity, native gameplay, GPU stereo, performance, comfort; no headline
green result hides missing coverage. Incomplete trace/footer or dropped events
invalidates claims for the affected interval.

## 4. A release must earn the user's time

- All features in its [master-plan gate](../README.md#delivery-three-meaningful-test-releases)
  work together. R1 is a playable sword/shield encounter, not a mono host test.
- Agent first reviews a reproducible ≥10-minute native session and a 20–45 second
  gameplay clip, plus stereo captures and machine results. Do not ask the user
  to collect F9 markers, configure guessed offsets or rediscover obvious defects.
- Supply one exact package/config, short controls sheet and a **3–5 minute**
  headset route with named new features and one feel/comfort question per segment.
  Human stops immediately for discomfort; failure does not earn repeated blind runs.
- Unique immutable version; payload SHA manifest; no game files, decompilation,
  credentials or user saves. Include current docs/licenses. No silent downgrade
  from physical combat to canned swings or from true stereo to mono.
- R3 additionally needs longer-session/area coverage, install/upgrade/uninstall
  rollback tests, supported runtime/controller matrix and documented exceptions.

## 5. Build and current-preview operation

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DDSVR_ENABLE_LAB=OFF -DDSVR_BUILD_RESHADE_ADDON=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\src\setup\Release\DSVR-Setup.exe --verify
.\build\src\setup\Release\DSVR-Setup.exe --doctor
```

MSVC C++20, static CRT; MinHook v1.3.4, OpenXR SDK 1.1.49, optional ReShade SDK
v6.6.2/API18. XR instance currently requests 1.0 for historical SteamVR support.
`--doctor` is read-only health information, **not offline/isolation certification**.
Default `tools/auto_verify.ps1` verifies, then always refreshes the overwritable dev test package `dist\DSVR-latest\` + `DSVR-latest-x64.zip` (+ `DSVR-latest-verification.json`, `DSVR-latest-ctest.txt`); any failed gate leaves the previous latest package untouched. Formal packaging still uses
`-Package -Version <new-version>` after the release gate. Formal outputs refuse
overwrite. No formal archive is refreshed with newer source under the same identity. Rate evidence: every run logs `RateMatch` (runtime period -> effective rate) and ~5 s `RateEvidence` (measured game FPS beside effective/runtime/requested); record both in native runs.

Historical previews are not recommended user tests. For a protected developer
deployment with DSR closed, setup supports `--install [--game <folder>]` and
`--uninstall`; `--yes` acknowledges prompts, not verified offline safety. Preserve
foreign proxies and `DSVR.toml`. Setup currently deploys loader/core only, **not**
the ReShade runtime/add-on. ReShade mode needs an explicitly pinned add-on-capable
host, matched core and `DSVR-ReShade.addon64`, with a reversible file manifest.

Current config truth: `src/common/config.h`, `config.cpp::ConfigSave`; GUI in
`src/setup/gui.cpp`. VR/headlook default OFF; XR input ON; `arm_ik_enabled=false`,
`arm_ik_log_only=true`. Keep those IK settings in historical previews: enabling
IK can suppress native melee without replacing it. `motion_attacks`/haptics
settings do not make missing native combat exist. Host selection:
`[vr] reshade_host=false` Present, `true` ReShade-only; startup immutable, no hot
fallback. Restart for host/action/resource changes; F7 isn't full reinitialization.

Internal desktop observation: set `[diagnostics] observer_only=true` **before
startup** in the protected lab. This overrides VR/headlook/body/pad/haptic settings
in memory without rewriting the file. F7 preserves that startup mode; F6/F8 never
call XR. F9 starts a ≤20-second trace even when verbose logging is false. There is
no render host/XR/GPU capture in this mode; use a separate verified GPU observer.
`camera_container`/`camera_slot` and `camera_follow_*` are raw native samples;
`camera_payload_3b/3e`, `camera_consume_3b/3e`, `camera_entity_matrix20/60/a0` observe
CPU acceptance. Their `seq` is a local sample association, not tracking/simulation
identity; zero `frame` means no bound dispatcher on that thread. None certifies
active camera selection, lifetime, GPU use or stereo. **Not save/network isolation.**

`constant_upload_enter/return` describe the natural CPU upload helper, not GPU
completion. `object` is its native buffer owner; `seq` associates this upload only;
`values[0..3]`, `[4..7]`, `[8..11]` are four 16-bit limbs each of source pointer,
native D3D context and buffer; `[12..13]` encode the raw 32-bit vector count in two
16-bit limbs. Flags 1/2 mark readable context/buffer identities, not non-nullness
or success. `constant_upload_block0/1/2` are the first complete 64-byte source
blocks when the count permits; flag 1 means readable, not necessarily a matrix.
At most 192 bytes are read, regardless of count. No inferred view/pass association
or HRESULT is attached. Existing dropped-event/incomplete-capture rules apply.

Current developer controls: left stick movement; right stick VR turn; A/B/X/Y,
menu and stick clicks through the pad bridge; squeeze/trigger map shoulders/
triggers, weapon attacks suppressed when IK enabled. F8 manual in-game menu
canvas; F6 canvas recenter; F7 reload; F9 log marker (bounded trace when verbose).
These are preview controls, not a promise of physical block/parry or automatic UI.

Support: retain `DSVR.log`, `DSVR-crash-*.dmp`, doctor output, config/build identity
and complete trace; analyze with `python -B tools/vr_trace.py <capture.jsonl>`.
For black image isolate source vs eye vs compositor with bounded readback. For
walking lag collect matched native views/root timing, not another smoothing guess.
If gating fails, stop rather than bypass it. Uninstall all test-owned proxies/
host/add-on files before online use; never use the lab save online. Steam file
verification does not guarantee removal of extra mod DLLs.
