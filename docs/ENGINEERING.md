# DSVR engineering contract

Current: 21 October 2026. [Documentation authority](../README.md#authority-and-maintenance). Task order and status belong only to the [execution guide](superpowers/plans/2026-10-21-native-conversion-execution.md). This file contains architectural constraints, not a second backlog.

## Native ownership

Use the existing native C++20/D3D11/OpenXR implementation, loader and native avatar. Preserve native world/AI/gameplay updates and local-player actor generation. New native writes/calls require a pinned executable and instruction signature, complete ABI, owner/lifetime, simulation-thread phase, side effects/refusal behavior and independent runtime correlation. Unknown offsets or truncated decompiler output cannot establish a binding.

Code discovery uses codebase-memory-mcp first; refresh missing/stale indexing. Locate exact callers/consumers before edits. Use rg for non-code text or insufficient graph results. Keep changes narrow and preserve the user's dirty work. Do not introduce a second avatar, replacement engine or generic automation framework.

## Frame and state model

Advance input/gameplay/physics/animation/combat/events once per native simulation update. Rendering two eyes must not advance those systems twice. Native draw workers and GPU resource lifetime must be proven separately from CPU return.

Publish coherent immutable snapshots containing time/sequence, tracking-space epoch, calibration revision, local actor/model/equipment lifetime and native update identity where needed. Do not retain transient pointers beyond their verified lifetime. Refuse stale, non-finite or incompatible state; reset episodes on focus/space/load/death/equipment transitions.

Distinguish physical tracking space, tracking-to-world mapping, native collision/motor root, HMD view, estimated body orientation and movement reference. Transform each quantity once and state its units/space at the boundary.

## Camera, stereo and locomotion

The view composes a gravity-aligned artificial yaw frame with full physical HMD rotation and direct physical translation. Body smoothing cannot delay or rotate the tracked view. Suppress native animation bob/shake/artificial pitch/roll; do not cancel physical tilt.

True stereo uses runtime asymmetric projections/eye poses, distinct scene targets and verified view-dependent resources/history, from one simulation state. Locate prediction before native command production. The existing XR begin/end eye API schedules images; it does not implement native scene rendering. Mono reprojection and AFR are optional tools, not completion of true stereo.

Head is the default movement reference and the always-available fallback. Controller mode supports selected/non-dominant reference and stable planar headings near vertical aim. The left stick is never left without a live reference: when the selected hand is untracked, or vertically aimed with no stable planar heading, steering returns to the head reference carried by the same fresh locomotion packet. The rearm latch gates the hand heading only, so a reacquired hand cannot yank a player already travelling head-oriented. Mode/reference changes latch at neutral. A stale or epoch-mismatched locomotion packet still neutralizes artificial translation. Physical samples continue during stick motion/turning.

The movement basis is the planar heading of the follow camera the mod actually wrote, never the rendered model's root facing. Native travel is camera-relative, so pre-rotating the stick into the model facing double-counts the turn the right stick already applied to the camera; the error equals (camera heading - model facing) and grows with every stick turn, which is the 23 October "walking the wrong way, worse after turning the camera, stuck against walls" report. The model facing stays a read-only validity gate and attribution signal (VR trace `camera_heading` records ChrPosData yaw, the model facing yaw and the steered heading together). Native's own movement turns the capsule toward the camera-relative request; direct player-facing or root writes stay out of policy until the E2 motor contract is verified.

Route physical follow through verified native movement/collision. Reconcile only attributable resolved physical motion against camera offset; mixed analog input alone does not prove attribution. Bound blocked residuals and reject forced-motion credit. Artificial turning pivots around the current horizontal HMD position. Direct root translation is not a replacement motor implementation.

Use separate verified solid/support/head queries. Obstruction protects against hidden-space visibility and through-wall reach even with comfort effects Off. Crouch must align native collision, native hurt geometry and visual pose, with clearance before expansion. Do not globally scale gameplay collision to repair visual calibration.

The current mono eye compositor applies `comfort_vignette`/`vignette_strength`
without changing projection. Input publishes artificial stick magnitude before
physical-follow mixing and the emitted turn delta. A predicted-time envelope
uses 60/250 ms attack/release, a 120 deg/s turn reference and up to 0.25 extra
demand during concurrent physical head rotation. The feathered mask uses each
eye's angular tangents and optical axis. Physical-only motion stays clear;
Off, theater, invalid tracking and failed submissions reset the envelope.
Preserve the native pixel constant buffer and its D3D11.1 range after drawing.
This implements comfort in the mono route; it establishes no native stereo or
obstruction binding, and tuning still requires the runbook's headset case.

## Body and equipment

Use measured native bone names/layout and a proven final pose-copy/skinning/attachment stage. Define owned bones and deterministic write order; preserve native collision/root and necessary animation state. Under animation ownership the IK is the sole writer of its owned rows: they restart each frame from a native source palette frozen once per rig revision, never from the previous corrected pose, while the native writer, animation state, legs and every non-owned row stay native and a malformed native row is still refused. Publish the committed weapon pose to combat; do not grow native gameplay logic inside a render/IK callback.

Calibration separates stature, reach, floor, posture and world scale. An explicit operator height trim is applied to the coherent mapping (view height and fit anchor together, so the camera stays on the native head bone) and is never written back into a saved profile. Capture once per launch after gameplay loads; retain measurements and physical origin through death/load/suspension. Explicit recalibration atomically replaces only a successful capture; cancel/failure retains usable prior data. Recenter changes view reference, not measured anatomy.

`calibration_fit.h` resolves one `CalibrationMapping`: native units per physical
metre = rig-bound native arm reach / estimated physical reach, multiplied by
the configured world-scale trim. Camera height, physical head/hand offsets,
grip offsets and roomscale velocity consume that same applied-camera snapshot.
Keep physical tracking poses and orientation axes unscaled. T/A captures use
full controller geometry with explicit anthropometric shoulder estimates;
horizontal A-pose spread is not arm length. Seated floor height does not shrink
reach or world scale. Rebind cached native geometry on model/resource/topology
changes and reject a fitted pose if its model revision changed after the view.

This changes apparent whole-world/native-avatar size together. It is **not** a
verified independent skinned-mesh resize. That render owner remains unknown;
do not scale palette bases, invent longer rotation-only links, change collision,
or reintroduce hand-only motion gain to conceal the gap. Native stereo eye
origins must eventually use this same mapping; no production eye-render binding
is established by the conversion math.

Align native weapons/armor and twist pairs; do not hide geometry by collapsing bones. Visibility filtering is independent of pose success. Estimated lower-body pose needs stable support/feet and bounded twist, not claims of measured foot tracking. Support-hand grip and native two-hand stance must agree.

## Combat, interactions and UI

Physical contact geometry follows the committed native weapon; swing energy follows valid head-relative physical tracking. Use real dimensions/axes and continuous rotational sweeps. Walking, turning, resting overlap, recentering and lost tracking cannot manufacture damage. Episode/lifetime identity prevents duplicate outcomes and costs across repeated renders.

Native damage, defense, stamina, poise, status and reactions remain authoritative. Direct health setters and unrelated AI event requests are not melee APIs. Correlate confirmed outcomes independently before issuing bounded per-hand haptics. Preserve native projectile/ammo/cast rules for ranged weapons.

Once the IK body owns the arms and native animation is blocked, every canned
combat output is suppressed, so the physical arms have to produce the input
themselves or the player cannot fight at all. Two opt-in bridges do that at the
pad seam (`src/core/swing_attack_math.h`, `src/core/shield_guard_math.h`): a
qualified weapon-hand swing becomes one press of the native light/strong attack,
and the shield hand holds the native shield button while it is raised and still
and taps the same button when it is flicked back to rest. Both emit ordinary
button lines and nothing else — no native damage, guard window, stamina or parry
judgement is invented or bypassed — and both fail closed on non-finite speed, a
non-monotonic clock, lost tracking, recentering, sanctuary, menu capture or
calibration capture. The guard is a head-relative height band plus a stillness
window, never a canonical guard pose, and hand speeds are head-relative so
walking manufactures neither action.

Interaction targeting uses tracked reach/aim, distance, line of sight and verified eligibility; per-action adapters invoke native actions. Explicit confirm controls are valid for scripted interactions and UI; gesture-triggered canned melee is not. Native UI may be hidden only when required replacement prompts/menus remain accessible. Do not claim simulation pause unless verified.

OpenXR suggested bindings must use components admitted by each interaction
profile. Touch has thumbstick axes/click/touch, not `thumb_up/down/left/right`
paths; names in a separate controller SDK are not OpenXR component names.
One invalid suggested path rejects the entire profile batch. Action sets become
immutable on attachment, and OpenXR allows one attachment per session: profile
edits require a game restart, never tearing down a working set to retry.
The persisted `binding_live_reattach` preference remains readable but cannot
opt into a forbidden second attachment.

Gameplay D-pad routing uses left stick-click plus stick direction before
locomotion orientation. It withholds movement and native L3 during selection,
requires stick centre after release, and preserves a neutral lone L3 on release
with a bounded 120 ms hold so slower native polls can observe it.
Home/recenter and recalibration chords take precedence. Native menus and spatial
UI retain direct stick navigation. This is a logical input route, not a fabricated
hardware D-pad action or evidence of native controller acceptance.

## Lifecycle and tool limits

One owner controls the XR frame lifecycle. Inactive frame work cannot block game loading/Present; preserve exclusive handoff and deferred safe teardown. Acquired images retain ownership through timeout/error until correctly waited/released. Restore only owned native/D3D/file state.

Use existing trace/profiling tools for bounded evidence. tools/native_stereo.py validates synthetic schema consistency; its success does not establish native stereo. WARP/fake-XR tests and pose math do not establish headset feel or native gameplay. Measure complete scene/eye-pair work and optimize observed costs rather than guessing.

Combat diagnostics use the existing bounded trace queue, not synchronous text
logging from pose/contact hooks. Admit each contact/identity/read-diagnostics
batch atomically or count all three records as dropped. The diagnostic writer
owns formatting, files and rate-limited `TraceHealth`/`CombatHealth` INFO lines.
Raw actor-side roles, HP/stamina deltas and return bits are observations, not
damage attribution. Provisional bench classifications remain explicitly weaker
than copy-verified weapon poses or native contact/defense outcomes.
