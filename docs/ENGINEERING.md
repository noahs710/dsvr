# DSVR engineering

[Master plan](../README.md) · All behavior below is a target unless marked current.
Implement inside the existing C++20/native modules; no replacement engine,
generic automation platform or extra approval/specification workflow.

## 1. Keep these boundaries

### Execution status — 21 October 2026

| Work package | Implemented and offline verified | Remaining acceptance / implementation |
|---|---|---|
| Calibration and IK | One-second stable arms-out capture once per launch; atomic profile; physical origin and stick heading persist through suspension/deaths/loads; seated reach estimates use arm span; vertical elbow pole and canonical wrist reference; native face/headgear/hair part filter independent of arm solves | Full-body retargeting and native head/helmet coverage acceptance. Forearm bone collapse failed live and was removed; forearm geometry filtering remains unbound. Input is held during setup; simulation is not paused |
| Home menu | Extensible native DX11 panels; apply/save/revert, controller navigation, bounded logging/filter/export; weapon placement preview/save | Native Home/panel rendering passed a protected title-screen smoke run; input blocking/theater still need acceptance. This is a game-image overlay, not a separate OpenXR HUD |
| Mono performance | One source copy; nonblocking GPU timestamps; nested CPU timings; restored DX11 state; stable cap under runtime throttling; fresh poses survive image failures; short camera/pose scopes reuse permission queries without caching data; NPC callbacks refuse equipment reads early; desktop Present skippable in VR (`desktop_present`); sub-stage timers split camera apply / target build / equipment / validation / commit | Desktop-Present-off run (19 Oct): menus 88–90 FPS, camera+targets 8.3→~4.1 ms; at ~53 FPS gameplay the remaining mod costs are camera apply 3.2 ms and arm equipment 1.7 ms. 90–120 Hz stereo target: reprojection route first (see stereo row); full-scene GPU coverage remains unqualified |
| UI and blur | Pinned UI draw suppression with theater restoration; native Blur getter/setter clamps; durable original preference and atomic INI replacement; uninstall stops on restoration failure | Native UI coverage and rendered blur proof, especially graphics-menu changes and cached flags |
| Two-handing | Real pose adapter invokes grip-held rigid-weapon solver; equipment/epoch checks; saved support position and weapon-axis wrist trims | Headset handling review; native two-hand stats/rules are not activated |
| Locomotion | Left-stick orientation is selectable as Head (default) or Controller; Controller uses the selected/non-dominant controller in the same tracking-to-world transform and neutralizes on missing/stale references. Config migrates legacy `movement_reference` and retires `head_directed_move=false`. Physical position samples continue while either stick is used and compose with analog translation. | Runtime changes latch on left-stick release. Headset qualification and collision-resolved body follow remain unverified; stick/physical pad-axis composition is not evidence of native motor ownership |
| True stereo / AFR | Token, distinct targets, eye-age, refusal and pair-submission contracts tested; menu labels unavailable modes. Ghidra corpus: engine emits per-view command streams (`EmitFrpgRenderViewCommands`; six-view cubemap pass exists), executed on workers | Production view-only draw replay, separate resources/history, native and headset proof; production remains mono. 90–120 FPS route: depth/rotation reprojection in the existing blit with depth captured via D3D11 context vtable at device capture; true stereo only after mono is fast |
| Physical combat | Tracked contact math and committed-pose proving ground with physical head-relative energy, corrected world direction, duplicate-frame handling and lifecycle/loadout resets; revalidated read-only equipment snapshots | Native collision extraction and qualified damage/block/parry ABI with native stamina/poise and event-driven haptics; no outcome dispatched |

The upgrade is incomplete until all implementation gaps and native
acceptance checks are satisfied. Static evidence and helper tests do not establish
runtime behavior. The green development package is for review and testing.

### Roomscale-native body follow — design for review

Physical horizontal HMD displacement should move the avatar through DSR's own
movement and collision simulation, not by directly writing actor/root data or
stretching the skeleton. Convert coherent, bounded roomscale motion into intent
through the existing XInput bridge; reconcile only DSVR-induced native root motion
against the camera's tracked head offset so movement is not counted twice. Refuse
roomscale output on stale tracking, focus/space-epoch change, recenter, teleport, or
unmatched root movement, falling back to current controls. Before enabling it, Ghidra
must establish the XInput-to-player-movement consumer and native root ownership; a
protected run must correlate tracking, input, root displacement, camera pose, and
collision response. Offline conversion tests are necessary but do not qualify body
follow. Do not add direct root/bone translation as a substitute.

This slice does not change avatar stature or global model scale, runtime FOV, arm
turn/tangle behavior, combat damage, two-hand profiles, or weapon-slot bindings;
these remain separate work after the native movement path is established.

### Remaining work plan — 17 October 2026

Task-level execution checklist, dependencies, file locations, commands and exit
gates: [detailed execution plan](superpowers/plans/2026-10-17-dsvr-execution-plan.md).

This sequence supersedes older execution notes below. The user requested planning
before further implementation, then approved execution. Integrated verification
is now 77/77 tests (`build/e1-ready-ctest.txt`). Inactive XR frame pumping runs on
a module-pinned worker with exclusive frame ownership and deferred session
teardown; actual mono-entry fixtures cover blocked Wait/End and visible handoff.
The inactive-only native run reached the title at 55–60 FPS with no available XR
system, and all 13 original save/install files were restored and hash-verified.
Connected invisible-session/wake acceptance remains separate from that result.

Use the native character, equipment and animation pipeline for embodiment, with
local geometry filtering for visibility. This preserves armor, weapons and game
rules. A separate duplicate avatar would add synchronization and asset problems;
moving the camera or collapsing bones cannot produce the requested embodiment.
Advance in the order below, with a concrete exit check for each stage.

1. **Unfreeze startup and headset recovery.** Finish and review the inactive XR
   frame pump. While the session is synchronized but invisible, run complete empty
   Wait/Begin/End cycles on a worker so neither blocking WaitFrame nor EndFrame can
   hold up native loading. Keep D3D, tracking, input and swapchains on their existing
   owner. Hand frame ownership back exclusively when visible/focused; defer session
   end/destruction until the worker is idle, without blocking Present or DllMain.
   Handle runtime errors, startup with no headset, sleep/wake, disconnect/reconnect
   and session loss. **Exit:** real logos/title/load remain responsive with the
   headset inactive, then enter VR correctly; fixtures force both Wait and End to
   block and verify exclusive handoff and shutdown.

2. **Establish a reliable performance baseline.** Use one disposable scene and
   unchanged Quest 3/VDXR Medium settings. Compare native baseline, immersive mono
   with head tracking, and IK on/off. Measure native camera/animation, mod tracking
   and solving, full scene GPU time, source copy/blit, XR waits/end, original
   Present and native limiter waits. Review the new state restoration, tracking
   retention and cap-stability fixes in the real game. If timing falls outside
   these stages, capture CPU wait stacks/GPU work rather than guessing. **Exit:**
   identify the dominant costs, record actual frame-time distributions and a
   repeatable comparison. Goals are stable 90 Hz (~11.11 ms/frame) and a qualified
   72 Hz option (~13.89 ms), not a claim inferred from requested settings.

3. **Lock calibration and scale into one body mapping.** Keep the once-per-launch
   setup, explicit Seated/Standing/Roomscale choice, release-then-hold trigger and
   atomic profile replacement. Death, loading, equipment changes, tracking loss
   and Home/theater must retain measurements, wrist reference, physical origin
   and intentional heading. Recenter changes the view reference; manual
   Recalibrate reopens setup and replaces measurements only on success. Separate
   measured floor height from estimated anatomy; a seated eye height must not
   imply a shorter torso/arms. Define a single physical-to-avatar scale shared by
   HMD translation, controller targets and stereo IPD. Use avatar rest dimensions
   and a standing reference or arm-span estimate for scale; seated floor height
   determines posture, not body size. Preserve native weapon reach/collision
   rules. **Exit:** seated/standing and short/tall reference captures produce
   consistent proportions; repeated deaths/loads do not accumulate offsets;
   failed/cancelled saves keep the last usable profile.

4. **Finish geometry visibility independently of IK.** Qualify the current native
   face/headgear/hair filter across equipment changes, death, human/hollow forms,
   supported model variants and theater transitions. Verify shadows/LODs and
   detect unsupported/custom geometry rather than assuming every base is empty.
   For forearm-only mode, filter actual triangles/mesh sections: keeping the arm
   armor slot alone is insufficient for the 58/222 stock arm archives containing
   upper-arm/clavicle weights. Build bounded per-resource caches and invalidate
   them on resource/equipment replacement; handle mixed-weight elbow boundaries
   without spikes or exposed seams. Preserve hands and held weapons/shields, and
   hide all other local-player geometry in that mode. Avoid modifying shared NPC
   resources or original game files. **Exit:** head/helmet never obstruct the view;
   forearm-only works for its declared armor coverage and never stretches skin or
   flashes the body when an arm solve is refused. Keep the option unavailable
   until its implementation passes that check.

5. **Build actual body embodiment.** Resolve a canonical eye-to-head offset from
   the avatar/rest data; there is no eye bone, so do not treat an animated head
   pivot as the eye position. Retarget the Upper_Root/Spine/Spine1/Neck/Head chain
   with joint rotations and anatomical limits, moving clavicle/twist/equipment
   descendants consistently. Solve arms from those corrected shoulders, remove
   the separate lean addition, and use the same camera-bound prediction/root.
   Handle body yaw, turning in place and pelvis/foot alignment so large stick turns
   do not become an impossible spine twist. Retain native gait/foot placement and
   collision authority; lower-body pose must be inferred because only head/hands
   are tracked. Rebind after native animation/stance changes without capturing
   another wrist neutral. **Exit:** horizontal, overhead, lowering, close-body
   reach, leaning/crouching and turning feel attached to one body; feet and
   shoulders remain plausible; weapon grips stay aligned through movement and
   respawn. An upper-body prototype alone is not full-body acceptance.

6. **Optimize mono and IK without reducing image quality.** Apply fixes supported
   by stage 2 measurements, then remeasure after embodiment. Cache validated rig
   topology, descendant sets and immutable grip data by owner/generation; remove
   repeated allocations, redundant transforms, full scans and unnecessary locks
   from hot paths. Keep one coherent pose per required native update, no second
   solve per eye, and exact invalidation on replacement. Preserve one source copy,
   asynchronous bounded diagnostics and native graphics state. Correct ownership,
   waits and frame pacing before considering resolution changes. **Exit:** matched
   before/after measurements show the gain, tracking stays responsive, and there
   is no lost detail, geometry instability or frame-rate-dependent arm behavior.

7. **Implement real stereo, then AFR and render-mode choices.** Bind a verified
   view-only native render boundary. Advance simulation, input, animation and
   combat once; render distinct left/right views from one prediction with correct
   asymmetric projections/IPD and coherent arms. Separate eye-dependent depth,
   culling, temporal histories and postprocessing while sharing work that is truly
   view-independent. Restore native state after each owned operation. First
   qualify True Stereo, then optimize its measured costs. Keep centered mono as
   the single-view performance mode. Add experimental AFR only with correct eye
   identity, age, pose/depth handling and recovery; assess its motion/judder
   tradeoff explicitly. **Exit:** native/GPU evidence shows distinct valid eye
   images, correct depth/occlusion and one simulation update; headset tests pass
   for head motion, moving hands and UI. Duplicating a mono image is not stereo.

8. **Finish native physical combat and two-handing.** Trace and qualify the game's
   weapon/contact/damage/block/parry APIs and actor lifetimes. Use swept blade and
   shield volumes, including rotation, from committed weapon poses. Exclude
   locomotion, recenter, teleport and tracking spikes from swing energy. Dispatch
   outcomes once per native update with per-target lifetime/contact budgets and
   native stamina, poise, defense, invulnerability and status rules. Implement
   directional physical shield blocking and timing/contact-based parry. Attach
   the support hand only while grip is held near the handle; preserve saved
   primary/support placements per weapon and release safely on grip/equipment/
   session changes. Add native two-hand stance/rules after verifying that bridge.
   Trigger haptics from confirmed outcomes. **Exit:** a supported sword/shield and
   two-handed encounter works at multiple frame rates, with no damage at rest,
   duplicated hits, direct HP edits or canned button attacks substituting for
   physical contact. Then qualify additional weapon families.

9. **Complete menu, onboarding and immersion UX.** Keep Home and the VR shortcut
   reliable in every supported state. Finish extensible settings/control/debug
   panels, validated Apply/Save/Revert, readable logs/export and actionable timing/
   tracking status. Qualify controller navigation, gameplay input capture, DPI and
   recovery from failed settings/calibration saves. Keep unsupported modes visibly
   unavailable. The launch calibration screen covers gameplay until completed or
   explicitly cancelled to a saved profile; do not claim simulation pause without
   a verified native pause. Hide native UI only in immersive VR when requested;
   theater reveals it normally and DSVR controls stay reachable. Close the blur
   enforcement gap when VR is disabled: installation/runtime must keep native blur
   off, including graphics-menu changes, and uninstall restores the original
   preference. Separately rendered VR HUD/UI remains the later milestone the user
   described, after stable core playback.

10. **Run the integration matrix and package the result.** Check cold/warm launch,
    active/inactive headset, reconnect, calibration/cancel/recalibrate, repeated
    death/respawn/load, equipment/armor/stance changes, tracking/focus loss, Home/
    theater/UI toggles and exceptional native camera/animation transitions. Cover
    seated/standing, overhead/lowered wrists, body visibility, native combat and
    every exposed render mode. Use identical scene/settings for 72/90 Hz timing
    reports; record actual cadence and CPU/GPU costs. Every native run isolates
    personal saves, retains the offline block and ends with exact restoration
    hashes. Refresh the development package only after the integrated build,
    relevant tests and doctor pass, with native evidence and honest remaining
    limitations. Publishing is a separate user-requested action.

Work can share research between stages, but headset acceptance proceeds through
startup/performance, calibration/visibility, embodiment, rendering and combat in
that order. Do not repeat a known-bad headset experiment without a concrete fix
and a regression or native observation that tests it. Review and measurement are
part of each stage, not substitutes for implementing its production path.

### Approved embodiment and controls upgrade — 16 October 2026

User approved persistent calibration, corrected vertical arm/wrist tracking,
forearm-only visibility, a Home-key settings/debug/logging menu, native physical
combat with grip-held two-handing, rendering/performance work, mandatory reversible
motion-blur suppression and optional immersive UI hiding. Target hardware:
Quest 3/Touch controllers, RX 9060 XT 16 GB, Ryzen 7 7700X, 64 GB DDR5,
VDXR Medium; measure 72/90 Hz rather than infer performance from the target.

Calibration setup opens once every process launch. A saved profile is loaded for
recovery/cancellation; successful launch setup or explicit Recalibrate replaces it.
Deaths and loads never reopen setup or replace measurements. Cancellation retains the old profile.
Temporary skeleton/space/attachment bindings may reset without changing saved
measurements. Recenter changes origin/heading, never reach or wrist alignment.
Launch calibration instructions cover gameplay until successful capture or explicit cancellation to a saved profile;
manual Recalibrate reopens the same screen. Never claim the game is paused unless
native suspension is verified. A tracking loss cannot approve or save calibration.

The menu registers independent panels/commands, captures gameplay input while open,
supports keyboard/mouse and controller navigation, validates settings before save,
and reads a bounded logging buffer. Grip profiles use weapon-local transforms and
stable equipment identity/handedness. Hold support-hand grip near the handle to
attach; release detaches. Preserve native damage/stamina/poise/parry rules and use
verified native outcomes, not direct HP edits or gesture-triggered attack animations.

Render modes are same-frame native stereo, centered mono and experimental
alternating-eye rendering. Native stereo/AFR require a verified view-only draw
boundary; unfinished modes must remain explicitly unavailable. Optimize shared
copies/diagnostics and measure actual CPU/GPU/pacing costs. Suppress motion blur
while the mod runs without losing the saved native preference. Hide native UI only
in immersive presentation, restore it in theater, and keep DSVR controls reachable.

Execution: retain a source snapshot; add red-capable regressions for lifecycle and
vertical motion; implement calibration/IK, menu, render and combat paths in parallel;
build/test the integrated source, then refresh the dev package only on a green gate.
Native/headset behavior needs separate protected runtime acceptance. This workspace
has no Git metadata; snapshots and verification receipts preserve the baseline.

| Existing code | Responsibility and current limitation |
|---|---|
| `src/loader`, `src/common`, `src/setup` | Forwarding, build gate, config, backups/installer; backups and acknowledgment do not enforce offline isolation |
| `render_host`, `reshade_bridge`, `src/reshade` | One startup-selected host; pin device/swapchain/thread, refuse races and invalidate resource generations |
| `xr.cpp`, `tracking_frame.h`, `input.cpp` | XR lifecycle, prediction and actions. **Currently predicts at Present, then blits one source to both eyes** |
| `camera`, `camhook`, `vr_heading` | Independent heading/calibration and follow-camera edits; invalid CameraMan container accesses removed, render-camera ownership remains unresolved |
| `arm_ik`, `arm_pose_math`, `presence` | Pinned post-animation position/grip adapter; arm alignment failed live, selective Head culling passed one scene. Whole-model hide remains an escape hatch |
| `hand_math`, `combat_math`, `pad_math`, `xinput_bridge` | Geometry/kinematics/math and native pad bridge. No native motion damage; legacy gesture classifier is not the combat design |
| `menu_state`, `menu_presentation` | Debounced manual/title quad. No complete native all-menu detector or HUD extraction |
| `vr_trace`, `lab_runner`, `lab_scenario`, `tools/vr_trace.py` | Bounded evidence/scenario seams. Production lab OFF; native safe-scene/profile adapters absent |

Game/animation/render owners are verified separately. Apply native edits only at
the owning natural callback; never call arbitrary game methods from an agent/XR
worker. Small bounded publication queues, no lock across XR/native calls, no file
I/O or GPU readback in hot animation hooks. Device migration/teardown is currently
incomplete: refusal plus restart is more honest than unsafe hot takeover.

Startup now publishes gameplay permissions after the selected render host is
installed. Only its accepted-device/poll callbacks consume those permissions
once; camera/presence/input/trace are not installed by the loader's startup
thread. This candidate addresses the observed startup stage; native recovery
remains unqualified. A failed host or observer/disabled/refused startup cannot
arm the callback, and reentrancy cannot acquire the hooks again. Later protected
runs reached gameplay after this deferral; it does not establish every startup
or quit/reload transition.

XInput retains its inline backend, with a game-local IAT fallback only after
persistent `MH_ERROR_MEMORY_ALLOC`. Discover one provider-resolved name/ordinal
import in the loaded PE; never patch a fixed RVA or chain an unknown owner.
Use native x64 CAS for the measured unaligned thunks, refuse cache-line
crossings, restore page protection and retain failed-cleanup state for retry.
The fallback pins its provider and DSVR through process exit: hot unload is not
supported. Uninstall disables overlay before conditional slot restoration;
late/in-flight calls retain the original export and pass through. This is tested
source behavior, not live controller qualification.

The inline backend also refuses a foreign existing hook and retains its owned
trampoline/module lifetimes across disable and late callbacks. Input samples
the right stick once per XR sync and shares that result between pad and turn
publication. A failed query clears queued yaw and snap state; successful
inactive actions retain the existing inactive-action policy.

## 2. Critical-path work packages

### A. Establish the real frame and camera owners

Run 6 now identifies natural selector/collector ownership across three actual
workers. TargetScene +0x128 selects one of three collector types; sampled world
entities dispatch through `0x1402161F0` / `0x140220000`. Main-camera identity is
shared across full/half-resolution, depth and color passes; a second Frpg camera
is auxiliary. Select by camera + targets + pass + owning context, never RTTI alone.
The OT collector mutates sorting/graphics state; its list lifetime and every
repeated-pass effect still need proof before eye replay. Observed PlayerIns /
ChrModel / ChrAsmModel paths narrow body integration, without proving bone writes.
Exact native observations and limits are in [recon](../MODDING_PLAN.md).

The legacy container-as-matrix path has been removed. No reads/writes at
CameraMan+0x30/+0x70 remain in the camera adapter; follow projection edits no
longer depend on that invalid reader succeeding. Present no longer reads native
camera memory. `CameraTraceFollowCam` now publishes a timestamped atomic title
heuristic from the natural committed follow matrix, and emits one basis log;
250-ms-old/invalid observations are unknown. The observer-only hook still returns
before this legacy VR branch. `test_camera_writes` tests actual adapter accesses
and projection restoration with bounded memory seams. Native scene/view ownership
and the frame timing problem remain open.

**GPU boundary now observed:** RenderDoc frame 2774 has 700 native draw calls.
At event 2579, VS slot 0 byte 0 supplies the 4×4 `gVC_WorldViewClipMtx` consumed
by SV_Position instructions. Reflection also places `gVC_CameraPos` at byte 64
and `gVC_LocalWorldMtxArray` at byte 448 (3×4, 38 entries in shader 1018).
Main scene color/depth and late orthographic HUD are visually separated; details
and capture-local IDs are in [recon](../MODDING_PLAN.md#1-target-and-cheapest-viable-route).
Main geometry uses native `0x14020dda0` (draw return `0x14020e5c4`) under camera
selection `0x1411c9740`. Do not turn this into whole-dispatch replay: history,
resource retention, camera-relative palette and auxiliary views still need ownership.
The sampled main matrix lacks ordinary world translation and camera position is
zero in that sample; camera-relative rendering is a hypothesis to resolve before
applying eye offsets, not permission to change only the first 64 bytes globally.

1. Resolve active render-camera ownership against the container/accessor evidence in
   [recon](../MODDING_PLAN.md#3-native-evidence-worth-retaining). Read-only native
   samples must establish active object, vtable/slot, camera variants and lifetime.
2. Capture D3D11 main-scene draws, constant buffers, final camera values, render
   targets and command-to-Present delay. Trace copied commands `0x3B/0x3E` to those
   consumers; exclude shadow/cubemap/UI/auxiliary views. Use RenderDoc first if
   compatible; otherwise a minimal bounded ReShade resource/constant census.
3. Verify native load/death/menu/equipment ownership. A zero blocker mask,
   non-null pointer, texture generation or XR-space epoch does not mean gameplay
   is safe. Observe natural Step calls; do not invoke stateful aggregators.

**Implemented, native-exercised:** `[diagnostics] observer_only=true` selects a
startup-locked desktop branch: no XR/render host, presence/pad installers or lab
runner. F7 cannot change its mode or re-enable write/input settings; F6/F8 do not
call XR. The natural follow hook samples only the bounded container/slot words
and committed follow transform/projection, returning before legacy camera writes,
restoration or IK. F9 installs observation hooks once after startup and starts
the bounded trace even without verbose mode; failed activation is not retried.
Startup itself installs no observer native patches. Early activation produced an
execute access violation at `0x180137d90`; deferred activation reached a loaded
scene and completed captures. This isolates a timing-sensitive activation fault,
not its low-level cause or a fix for the legacy VR startup branch.
This is read-only with respect to gameplay, **not** a save/network sandbox or a
verified final GPU observer. Temporary observer deployment has been restored.
Unfiltered upload events overwhelmed the queue (2,483,135 drops in capture 2);
do not infer paired calls, full timing or main-scene identity from that trace.

Static pinned bytes now locate `HGCameraEntity`'s `0x3B/0x3E` dispatcher and CPU
matrix destinations ([recon](../MODDING_PLAN.md#3-native-evidence-worth-retaining)).
The byte/vtable-gated trace records raw payload/cursor/handle association and
post-original matrices without replay or edits. Fixture tests exercise the actual
detour and writer; native command acceptance, final GPU constants, main-scene
selection and delivery delay still need the protected experiment.

Follow-up static work connects context camera selection (`+0x158`) and draw
constant preparation to `0x140CD0A10`'s Map/copy/Unmap helper and VS/PS bindings.
The observer now hooks that natural helper, forwarding all four arguments exactly
once. It records buffer/source/context identities, vector count and at most the
first three complete 64-byte source blocks, before the native call; post-return
events retain capture ownership. No readback, extra Map, native mutation, render
host or XR is introduced. Shader callbacks may change prepared constants, so
observe this upload boundary rather than assuming context blocks remain final.
This proves neither native Map success nor draw targets, shader consumption or
GPU completion. Match these identities and values in the bounded native capture
before selecting any eye-render seam; the current added hooks are not deployed.

**Exit:** one causal native command → consumed matrix → main-scene image chain,
with measured thread/frame ownership. Keep unrelated existing behavior intact
until its replacement has an oracle; no new writes through disputed fields.

### B. Two real views, one simulation instant

Mono image recovery now retains each acquisition until a successful wait/release
or actual swapchain destruction. Timeout never permits a blit or release;
failed release suppresses the layer. First waits are bounded at 5 ms/eye and
cleanup polls are nonblocking. A teardown-only D3D11 event marker proves queued
GPU commands completed before owner destruction; failed cleanup retains that
marker/resources and prevents image reuse. Public entry points pump pending
destruction before image drains, including after stop/restart. Session stop
alone does not clear held leases. This follows the
[OpenXR wait contract](https://registry.khronos.org/OpenXR/specs/1.0/man/html/xrWaitSwapchainImage.html)
and [destruction contract](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrDestroySwapchain.html).
The actual mono path is exercised with fake XR/headless WARP; native stereo
rendering remains unbound.

Current source has `XrBeginNativeStereoFrame` / `XrEndNativeStereoFrame` and
`XrStereoFrame` for prediction, distinct borrowed targets and bounded image waits.
No native renderer calls that API yet; Present still uses the mono path.
`stereo_render.h` now owns the bracket contract (one token, distinct targets,
each eye at most once, mask-3 submit, whole-pair cancel) behind a draw
callback seam, and `display_rate.h` owns runtime-driven effective-rate
matching (`RateMatch`/`RateEvidence`); both are source-tested. The proven
native draw span (below) is still required before any hook installs the
bracket -- live output remains mono until then.
`stereo_math.h::NativeEyeClip` builds per-eye shader rows from the observed
camera-relative LH projection, an explicit eye origin/basis in center-view axes
and asymmetric eye tangents. It preserves native forward-Z depth coefficients,
removes center off-axis terms before extracting rotation, and rejects translated,
non-perspective, invalid or reversed-Z inputs. Failure clears its output.
The caller still must establish pose/scale/pass ownership. No new native writes
or render replay are enabled by this helper.
Regression inputs include measured frame-2774 shader rows and event-2579 GPU
position, plus parallax, frustum-edge, canted-eye and refusal cases. Offline
post-VS samples are retained in `build/renderdoc-relative-camera.json`; these
do not certify a live eye pair or the absolute-world origin.

- Split the monolithic mono path into a small XR begin/predict → native eye pair
  → submit lifecycle. Predict before command construction for either eye. Retain
  source simulation/render-plan state through both views and their copies.
- Prefer a bounded native view-only bracket **if demonstrated**. If DSR requires
  broader replay, enumerate and suppress its non-render effects selectively.
  Whole dispatcher replay, zero dt, scratch rewind or legacy stereo activation
  are not accepted shortcuts. Do not swallow native assertions.
- One frame prediction/reference space; distinct eye views and asymmetric FOVs.
  Keep capture, native scene/load, simulation, resource, tracking and space IDs
  separate. Carry source/consumed-view/image ownership to submission.
- No duplicate input, physics, animation, combat, RNG side effects, history
  rotation, resource retirement or Present. Join actual workers and preserve
  native/GPU resource lifetimes; command return alone is not image completion.
- Restore only owned edits whose owner/lifetime and expected value still match.
  Transition/conflict/timeout invalidates the whole pair. Keep cinematic fallback
  separate from stereo success; never pair one new eye with one stale eye.
- Implement acquire/wait/release state handling, bounded failure recovery and
  device/session shutdown correctly. `XR_TIMEOUT_EXPIRED` is not image readiness.
  Avoid mandatory per-frame CPU/GPU synchronization in the shipping path.
- Measure CPU/GPU eye-pair cost before pacing changes. Preserve native gameplay
  speed; rendering above the game's normal simulation rate requires a verified
  decoupling/interpolation policy. A limiter patch alone is not that policy.

**Exit:** real stereo evidence, preserved simulation speed and transition tests;
no synthetic validator can supply missing native observations.

### C. Native body, not proxy hands

Current position adapter: natural `0x140320980` calls original animation once,
then corrects the selected 80-entry model-space MdlMtx before the measured body
copy. Bone names/parents are resolved from the current typed FLVER owner; 48-byte
poses, root and cached records are checked again before committing. The solver
moves arm descendants/twist bones while retaining native grip/finger stance.
Targets use the exact camera-bound tracking sequence/space, with offsets relative
to its locomotion anchor; current root basis converts those offsets to model
space without large-world subtraction. Stale/menu/untracked/foreign-thread samples
and changed ownership are refused. Install, shutdown and commit are serialized;
the trampoline survives in-flight callbacks. No file I/O occurs in the pose hook.
Grip rotation follows a captured reachable native stance; weapon-bone offsets
keep the controller target at the native grip rather than the wrist joint.
Configured mirrored roll is provisional and has no headset acceptance.
Release suite: 53 source tests, including real-adapter ownership/refusal and
grip/weapon fixtures. Live arm alignment failed despite successful CPU commits.
Selective Head-bone collapse cleared the view in the tested scene; LOD/helmet
coverage and torso correction remain unfinished. Defaults are
`arm_ik_enabled=false`, `arm_ik_log_only=true`.

- Find player pose-array owner/stride/bone mapping and last animation → model →
  skinning/attachment stages. Establish normal/alternate player models and all
  equipment cases. Use known bone names plus measured transforms, not fixed guesses.
- Capture a native stance per equipment/stance, keep live finger grip, solve
  shoulder/elbow/wrist chains, spine/root yaw/shift and head clearance. Apply
  idempotently after every required pose evaluation, before palette/attachments.
  Do not apply a body correction once per eye or freeze collision/root motion.
- Use the **same consumed rendered-root frame** for camera and hand targets.
  Separate locomotion/collision authority from view heading; preserve physical
  HMD rotation/translation. Do not smooth world/root movement to hide phase lag.
- Bind native weapons/shields and their damage capsules to the solved bones.
  Verify hand-grip alignment with the equipped asset, not assumed global grip -Z.
  Hide only local head/neck/helmet geometry across LOD/blur variants; no forced
  invisible armor, whole-body hiding or unverified far-away dummy edits.
- Invalidate stance/pose caches on load/death/equipment/model/space replacement.
  Support seated reach/height without changing weapon reach/damage unfairly.

**Exit:** rendered native arms and weapons follow tracked targets while walking,
turning and changing stance. Solver logs or screenshots of debug proxies fail.

### D. Physical combat through the native game

- Trace a normal DSR attack: owning actor/behavior/weapon → attack parameters →
  capsules → contact → damage/stamina/poise/reaction. Recover ABI and stable
  instance identity before injecting events. Run gameplay effects once per
  simulation update, never once per eye or blindly per XR render.
- Contact follows the solved native blade, not a controller-near-enemy shortcut.
  Cover hilt/tip motion **and rotation** with swept volumes/substeps. Existing
  capsule-plus-tip-sweep math is a useful approximation, not proof against every
  rotational tunneling case or native collision shape.
- Scale committed hits from weapon-relative motion/contact direction and bounded
  energy/damage budgets. Exclude world locomotion, snap turns, recenter/teleport
  and tracking spikes from swing energy. Preserve native defenses, stamina,
  status effects, poise and invulnerability semantics; no direct HP pokes.
- Continuous contact must have time-normalized totals, a per-target **lifetime**
  identity and first-impact reaction policy. Rehit cooldown alone is not a
  rate-independent contact budget. Avoid double-charging stamina or applying
  poison/bleed every render. No damage at rest or from lost tracking/menu hands.
- Shield pose determines facing/coverage, not simply “off-hand is high.” Physical
  parries require valid native timing/contact/direction; assistance is optional.
  Low stamina changes combat response/visual compliance, not impossible hardware
  force feedback. Buttons remain useful for rolls, items, ranged release and UI.
- Send confirmed hit/block/parry/hurt feedback to a prioritized, bounded haptic
  envelope per hand. Stop/cancel on focus loss, menu/disable and tracking failure.
  Never use unrelated HP loss as sole attribution for our hit.

**Exit:** one supported sword/shield encounter with native outcomes, fair costs,
no canned melee, no frame-rate-dependent repeat-hit exploit. Then add families.

### E. Usable VR, then breadth

Separate world from UI at verified DSR UI draw/tonemap boundaries; account for
blend/depth and postprocess differences. Use black/white capture only when its
src-over assumptions hold. World-anchored readable menus, independent HUD,
controller navigation and automatic transitions must work without desktop keys.
Do not claim “pause” unless native simulation actually pauses.

R2 adds calibrated two-hand/dual-wield/ranged/catalyst paths while preserving
equipment/projectile/ammo rules. R3 handles ladders, rolls, backstabs/ripostes,
death/respawn, cutscenes and exceptional cameras with deliberate comfort
presentation. Test the behavior rather than inventing DSR versions of ER features.
Native Win32 setup stays dependency-light, keyboard/DPI/high-contrast accessible;
in-headset UI is readable and stable, not a webview or effects showcase.

## 3. Keep existing tools subordinate to the product

`tools/native_stereo.py` / `tests/test_native_stereo.py` define the strict
synthetic schema in executable form: 13 ordered phases, stable identities/effect
counters, predicted/consumed matrices, worker joins, image association and owned
restore. Reader limit 256 KiB; budget ≤20 s; exact fields only. CLI exit 0 means
**synthetic consistency**, always `complete=false`, native/GPU/comfort UNVERIFIED.
There is no native emitter. Retain its regression coverage; do not build another
acceptance framework or ask for a headset run to exercise this fixture.

Use the current trace writer/runner for targeted observations, adding only what
the next native experiment needs. `skeleton_scan.py` checks log plausibility,
not bone ownership. Math tests must not become headline feature completion.
