# Continuous XR hand calibration

Date: 2026-11-01

## Goal

Keep the native first-person hands and held weapons aligned to the Quest controller grip poses through initial setup, gameplay, respawns, and OpenXR reference-space changes. Calibration starts before the game menu and remains a full immersive room. The system must never learn from a stale or inferred hand pose.

## Existing evidence and cause

The current capture stores neutral controller rotations after removing the headset yaw sampled during setup. Gameplay later removes `g_q0`, which is first captured when a native player/camera anchor binds. Calibration happens before that bind, so the saved neutral pose and live controller pose can use different yaw frames. The last headset log confirms calibration ran with no camera-applied sequence, then gameplay bound later. The frame audit is in [the research note](../../research/2026-11-01-calibration-frame-audit.md).

The OpenXR grip pose is the standard full-grip pose for holding and swinging a sword. Pose validity and pose tracking are distinct; only tracked poses can contribute calibration evidence. See the [Meta OpenXR input guide](https://developers.meta.com/vr/documentation/native/android/mobile-openxr-input), [Meta action-space guide](https://developers.meta.com/vr/documentation/native/android/mobile-openxr-actions-actionsets-bindings), and [Khronos OpenXR 1.1.65 specification](https://registry.khronos.org/OpenXR/specs/1.1/html/xrspec.html).

## Design

### Pose ownership

- Use the existing OpenXR `LOCAL` space for the HMD and controller grip poses. Seed one session yaw origin from the first tracked HMD pose before calibration begins; capture and live refinement use that origin. Persist each alignment's yaw reference, then convert it into the camera's current reference when arm IK compares poses. This preserves correct orientation if gameplay binds later or the user recenters. All poses in a sample use the same predicted display time.
- Preserve per-hand position/orientation `valid` and `tracked` state through acquisition. Untracked-but-valid poses may render for continuity, but may not seed or refine calibration.
- Treat `XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING` as a frame change: stop sampling, rebase the session transform from the new reference, clear transient estimator windows, and keep saved user measurements unchanged.
- Keep the native grip-to-wrist offset keyed to both calibration revision and native rig revision. Derive it from the first validated, animation-free native bind once that rig exists. A respawn may refresh the native offset but cannot rewrite user anatomy.

### Calibration data

- Separate user anatomy (eye height and reach estimates) from controller-to-native-hand alignment.
- Store neutral grip orientations in the yaw-neutral session representation, with the frame convention named in the profile. Bump the profile version. Migrate v1 body measurements, but invalidate its ambiguous neutral wrist quaternions so they cannot recreate the backwards-hand defect; prompt for a short alignment capture while preserving height/reach.
- Continuous refinement changes the live session hand alignment only. The saved profile changes only when the user accepts the preview in the calibration room.

### Continuous refinement

- Use the OpenXR grip poses, not aim poses.
- Admit a refinement window only after at least one second in a broad playstyle-appropriate rest region with low controller motion. Both controllers must be valid, tracked, fresh, the session focused, and the reference-space epoch unchanged. Seated lap and relaxed standing poses are accepted without exact T/A angles.
- Suspend refinement during locomotion, attack/defense, tracking loss, menu/calibration transitions, loads, respawns, and reference-space changes. Never train from a single frame.
- Smooth and cap the live orientation correction; decay no correction from untracked data. Do not scale bones, rewrite colliders, or infer elbow/shoulder locations from controller-only input.
- If no safe neutral window appears, hold the last good alignment and show a small non-blocking status cue; do not force a calibration step during combat.

### Calibration room and flow

- Keep the sequence `Calibration → Game Menu → Loading → Immersive Gameplay`; the room remains inside the immersive OpenXR frame owner, never a quad/theater layer.
- Preserve the dark green fog, transparent poster-style guidance, eye-level layout, and selected-playstyle recommendation.
- Show a live, rig-independent preview: tracked grip markers follow the controller positions and wrist-axis ticks show their orientation on the poster. Accept a broad comfortable posture, require both hands tracked, and keep the existing one-second both-trigger action.
- Show when the live fit is stable and let the user explicitly accept/save user alignment in the room. Rig-derived offsets remain separate and are recomputed from the validated bind when the rig revision changes. Skip/cancel preserves the last saved anatomy and alignment.

## Failure behavior

- If tracking is invalid, untracked, stale, or changes reference space, freeze updates and retain the last good live fit. Never convert tracking loss into a new neutral pose.
- If the v1 profile cannot be safely migrated for wrist alignment, preserve its body measurements and prompt for the short capture. If the user skips, ignore the ambiguous v1 wrist quaternions and use the validated native bind-derived grip mapping for that session.
- Calibration errors must remain visible in the room; they must not blank or replace the XR frame.

## Completion evidence

- Static and focused verification should cover shared-frame conversion, quaternion sign handling, untracked-pose rejection, epoch changes, rig rebinds, v1 migration, bounded refinement, and save/cancel behavior.
- Final headset qualification requires the user to launch through Steam/SteamVR and confirm neutral alignment, relaxed wrist bends, attack/defense motion, respawn, and recenter. The agent will not launch the game without a separate request.

## Scope boundary

Controller-only tracking cannot observe the wearer's elbow or shoulder joints. This change improves measured reach use and tracked hand alignment; it does not claim exact anatomical joint reconstruction. Physical hand positions continue to follow controller grip positions.

## Implementation status

- Calibration profile v2 stores wrist alignment separately from body measurements, including its yaw reference. Version 1 body measurements migrate; the unframed wrist quaternions are discarded.
- The live calibration poster follows tracked controller positions and orientation. Quiet-play refinement is session-only, limited to two tracked wrists in a relaxed region, neutral gameplay inputs, and a stable one-second window. It applies small bounded changes; a disagreement over 45 degrees asks for a fresh explicit capture.
- Native neutral-hand offsets are cached by both calibration and rig revision. Respawns can refresh the native offset without changing saved user measurements.
- The Release `dsvr_core` target compiled successfully and the matching DLL is staged in the game folder with a dated rollback copy. No tests were run and no game was launched. User headset acceptance remains pending.
