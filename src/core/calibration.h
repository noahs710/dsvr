#pragma once

#include <cstdint>
#include <string>

namespace dsvr {
struct TrackingFrame;

// User measurements only. No native objects, world coordinates or animation
// matrices belong here; loading a new scene must not change this profile.
struct UserCalibration {
  std::uint32_t version = 2;
  bool valid = false;
  bool floor_height_measured = false;
  float eye_height_m = 1.65f;
  float head_to_arm_m = .25f;  // anthropometric shoulder-plane estimate
  float shoulder_half_width_m = .17f;  // anthropometric estimate, not measured
  float arm_reach_m[2] = {.55f, .55f};  // inferred shoulder -> controller reach
  float arm_span_m = 1.44f;  // inferred full extension, not A-pose hand spread
  // Controller grip quaternions in the alignment reference's yaw-neutral frame.
  float neutral_controller[2][4] = {{0,0,0,1},{0,0,0,1}};
  float alignment_reference_yaw[4] = {0,0,0,1};
  bool hand_alignment_valid = false;
  std::uint64_t revision = 0;
};

// View height is distinct from anatomical size. Sitting changes the eye's
// floor-relative position, not reach or stature. A legacy numeric fallback
// never overrides a usable calibration without the explicit opt-in.
inline float CalibrationViewHeight(const UserCalibration& profile, float configured,
                                   bool /*seated*/, bool manual_override = false) {
  return profile.valid && !manual_override ? profile.eye_height_m : configured;
}

enum class CalibrationPhase { Loading, WaitingForTracking, AwaitingPose, Holding, Ready };
struct CalibrationStatus {
  CalibrationPhase phase = CalibrationPhase::WaitingForTracking;
  bool profile_valid = false;
  bool screen_visible = false;
  bool capture_input = false;
  bool pose_ready = false;
  bool saving = false;
  bool save_failed = false;
  bool can_cancel = false;
  bool hand_alignment_valid = false;
  bool hand_alignment_saved = false;
  bool refinement_review_needed = false;
  bool hand_preview_valid[2] = {false, false};
  float hand_preview_position[2][2] = {{0,0},{0,0}};
  float hand_preview_angle[2] = {0, 0};
  float hold_progress = 0;
  const char* instruction = "Waiting for tracking";
  // Short live tracking guidance. The recommended pose never blocks capture.
  const char* pose_hint = "";
};

// Start/stop on the owner worker. Loading and atomic saves use a dedicated
// worker; tracking/camera/native-pose callbacks never perform filesystem I/O.
void CalibrationStart(const std::wstring& profile_path);
void CalibrationStop();
void CalibrationRequest();
void CalibrationCancel();
void CalibrationRecenter();
std::uint64_t CalibrationRecenterGeneration();
UserCalibration CalibrationCurrent();
CalibrationStatus CalibrationGetStatus();
bool CalibrationCapturesInput();
// Calibration begins as soon as the XR session can present a visible frame;
// it does not wait for a game camera or the native main menu.
void CalibrationPresentationReady(bool ready);

// Call once after publishing a coherent physical-left/right TrackingFrame.
// presentation_ready means the XR session can display the calibration scene.
// LOCAL head Y is not floor height; supply a measured floor reference
// only when the runtime actually provides one. Otherwise preserve setup height.
// play_style selects a recommendation ("standing", "seated" or "roomscale").
// Any comfortable, tracked two-hand pose is accepted; wrist orientation is
// sampled as held, so the rendered hand follows the controller 1:1.
void CalibrationSample(const TrackingFrame& frame, bool both_triggers_held,
                       bool presentation_ready, bool floor_height_valid = false,
                       float floor_eye_height_m = 0,
                       float fallback_eye_height_m = 1.65f,
                       const char* play_style = nullptr);

}  // namespace dsvr
