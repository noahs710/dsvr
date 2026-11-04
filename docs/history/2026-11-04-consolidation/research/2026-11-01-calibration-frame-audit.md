# Calibration frame audit and continuous-calibration research

Date: 2026-11-01. Read-only audit of the 1 Nov Quest Link / SteamVR session and current calibration path. No source, config, saved profile, or game installation was changed. The game process is closed; SteamVR `vrserver` remains running.

## Observed session

- `metavr 1.10.0.30.10` reports the connected device as Quest 3 (`eureka`, Android 14). The game log identifies SteamVR/OpenXR in Meta compatibility mode 2.18.4.
- In `DARK SOULS REMASTERED/DSVR.log`, calibration ran before the gameplay camera bound (`camera_applied_seq_at_entry=0`). At 16:38:06.591 both triggers were down and the one-second capture showed a seated lap pose (hands about 0.44 m apart and 0.49 m below head height). Gameplay had not begun at that point.
- The capture now accepts any finite two-controller pose. Its hint recommends a lap pose for seated mode, but it does not constrain wrist angle or show a live hand/blade alignment preview. A later calibration request began at 16:40:51.850 with an arms-out span of 1.46 m. The saved profile was last written at 16:40:52 UTC/local conversion consistent with this second attempt; the log does not record the final completion frame.
- The first gameplay rig reported both native bind hand bases 96.4 degrees from the generic T-pose guess. The log reports calibration revision 3 later in the session. These are observations, not proof that the native bind basis itself is wrong.
- The session submitted VR frames without image timeouts. This audit does not qualify rendered hand alignment or subjective comfort.

## Strongest code-level cause: inconsistent yaw references

`CalibrationCapture::Feed` removes the headset yaw observed during calibration before saving each controller quaternion ([calibration_math.h](../../src/core/calibration_math.h#L131)). Gameplay arm IK later removes `g_q0` from live controller orientation ([arm_ik.cpp](../../src/core/arm_ik.cpp#L1665)), but the calibrated neutral quaternion is interpreted directly in the camera body's basis ([arm_ik.cpp](../../src/core/arm_ik.cpp#L1673)). `g_q0` is captured only when the first native player/camera anchor binds, after calibration ([camera.cpp](../../src/core/camera.cpp#L368)). No calibration-reference yaw is persisted in `UserCalibration` ([calibration.h](../../src/core/calibration.h#L11)). Therefore calibration and live poses can be compared in different yaw frames if the player's heading changes between setup and first camera bind. The predicted result is a common wrist/weapon rotation error equal to that heading difference; an approximately quarter-turn can look like a sideways or backward hand.

This is the leading hypothesis, not a confirmed runtime root cause. It can be disproved by recording both reference yaws and showing they agree throughout the failing path.

## Other falsifiable causes

1. **Inferred controller pose admitted during capture.** Acquisition requires OpenXR `POSITION_VALID` and `ORIENTATION_VALID` but does not retain `*_TRACKED` flags ([input_frame_acquisition.cpp](../../src/core/input_frame_acquisition.cpp#L99)). Meta documents that a pose can remain valid while not tracked, for example when the runtime estimates it. Prediction: a capture with either controller untracked can preserve a stale orientation; a tracked-only capture would change or reject that profile.
2. **Neutral native basis survives rig rebind.** `NeutralHandBasisCache` is keyed by calibration revision, not rig/model revision ([arm_pose_math.h](../../src/core/arm_pose_math.h#L352)); rig binding clears the source reference but does not reset that cache ([arm_ik.cpp](../../src/core/arm_ik.cpp#L339)). Prediction: if a respawn replaces the native hand basis under the same calibration revision, the cached basis stays old; resetting it on rig revision changes alignment.
3. **Unrestricted capture commits an unintended neutral wrist.** The one-second gate accepts any tracked, steady pose and treats the playstyle instruction as advisory ([calibration_math.h](../../src/core/calibration_math.h#L148)). Prediction: a pose that is comfortable for the hands but poorly aligned to the controller's grip pose is saved as neutral; a live preview that shows grip/blade alignment before commit makes this visible.

## Primary-source guidance

- Meta's [OpenXR Input API](https://developers.meta.com/vr/documentation/native/android/mobile-openxr-input) distinguishes pose validity from tracking: valid data may be inferred while tracking is degraded. Calibration-quality samples should require tracked orientation and position; valid-but-untracked poses can still be displayed but should not refine the profile.
- Meta's [OpenXR action and binding guide](https://developers.meta.com/vr/documentation/native/android/mobile-openxr-actions-actionsets-bindings) defines action spaces as poses located relative to an explicitly supplied reference space. The app must keep calibration and live pose transforms in the same reference frame.
- Khronos [OpenXR 1.1.65](https://registry.khronos.org/OpenXR/specs/1.1/html/xrspec.html) describes the grip pose as the full-grip pose for holding and swinging objects, and separately defines valid versus tracked location flags and reference-space changes. It is the appropriate controller pose for sword alignment; aim pose is for pointing.
- Meta's legacy [PC Touch controller tracking page](https://developers.meta.com/vr/documentation/native/pc/dg-input-touch-poses) also states controller poses share a tracking frame with the headset, but the OpenXR spec is the primary authority for this app's input path.

## Direction to settle before implementation

Use one explicit XR reference for every captured and live pose; preserve calibration anatomy separately from runtime hand alignment. Feed continuous refinement only from confidence-qualified tracked grip poses and stable, low-motion windows. Keep pose changes bounded and make the result previewable before it affects the saved profile. The remaining product choice is whether updates should happen automatically during gameplay, only in a visible calibration mode, or both.
