#pragma once

#include <cstdint>
#include <string>

namespace dsvr {
struct TrackingFrame;

// User measurements only. No native objects, world coordinates or animation
// matrices belong here; loading a new scene must not change this profile.
struct UserCalibration {
  std::uint32_t version = 1;
  bool valid = false;
  bool floor_height_measured = false;
  float eye_height_m = 1.65f;
  float head_to_arm_m = .25f;  // anthropometric shoulder-plane estimate
  float shoulder_half_width_m = .17f;  // anthropometric estimate, not measured
  float arm_reach_m[2] = {.55f, .55f};  // inferred shoulder -> controller reach
  float arm_span_m = 1.44f;  // inferred full extension, not A-pose hand spread
  // Controller grip quaternions in the calibration head's yaw-neutral frame.
  float neutral_controller[2][4] = {{0,0,0,1},{0,0,0,1}};
  std::uint64_t revision = 0;
};

// Seated users keep the configured avatar eye height. The measured floor-to-eye
// value describes their physical pose; it must not put the camera in the adult
// model's torso. Standing/roomscale users use their measured height.
inline float CalibrationViewHeight(const UserCalibration& profile, float configured,
                                   bool seated) {
  return profile.valid && !seated ? profile.eye_height_m : configured;
}

enum class CalibrationPhase { Loading, WaitingForGameplay, AwaitingPose, Holding, Ready };
struct CalibrationStatus {
  CalibrationPhase phase = CalibrationPhase::WaitingForGameplay;
  bool profile_valid = false;
  bool screen_visible = false;
  bool capture_input = false;
  bool pose_ready = false;
  bool saving = false;
  bool save_failed = false;
  bool can_cancel = false;
  float hold_progress = 0;
  const char* instruction = "Waiting for gameplay";
  // Live T-pose guidance: which acceptance bound is failing right now,
  // so the user can correct their stance instead of guessing.
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
// Refresh the load gate before drawing the accepted game image, so the first
// ready gameplay frame cannot appear before the calibration instruction screen.
void CalibrationGameplayReady(bool ready);

// Call once after publishing a coherent physical-left/right TrackingFrame.
// gameplay_ready means a native player/camera is ready, not merely a live XR
// session. LOCAL head Y is not floor height; supply a measured floor reference
// only when the runtime actually provides one. Otherwise preserve setup height.
void CalibrationSample(const TrackingFrame& frame, bool trigger_held,
                       bool gameplay_ready, bool floor_height_valid = false,
                       float floor_eye_height_m = 0,
                       float fallback_eye_height_m = 1.65f);

}  // namespace dsvr
