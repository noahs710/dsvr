#pragma once
#include <cmath>

namespace dsvr {

// Pure, unit-tested stick turning (docs/TESTING.md: right stick
// turns; snap by default, smooth optional). OpenXR action reading stays
// outside; this module owns the two decisions that feel broken when they
// are wrong and cannot be tuned without a headset once wired:
//   - a held stick must fire ONE snap, then stay silent until the stick
//     returns past a lower re-arm threshold (no machine-gun snapping);
//   - smooth turn must start at zero exactly at the deadzone edge
//     (rescaled), or every turn begins with a lurch.
//
// Conventions:
// - stick_x is -1 (full left) .. +1 (full right), one sample per frame.
// - Returned yaw delta is in radians; positive = turn right. The camera
//   consumer maps this sign onto its basis rotation (camera_math.h
//   ApplyYawPitch); do not flip it here to "fix" a wiring bug later.
// - If snap and smooth are both enabled, snap wins (the shipped presets
//   set exactly one; snap is the comfort default).

struct TurnParams {
  bool snap_enabled = false;
  bool smooth_enabled = true;
  float snap_degrees = 45.0f;         // [vr] snap_turn_degrees, 15..90
  float smooth_speed_dps = 360.0f;    // [vr] smooth_turn_speed_dps, matches Config
  float deadzone = 0.25f;             // |stick| below this is centred
  float snap_trigger = 0.5f;          // snap fires crossing this
  float snap_rearm = 0.3f;            // re-arms below this (< trigger)
};

class TurnTracker {
 public:
  explicit TurnTracker(TurnParams params = {}) : params_(params) {}

  // Yaw delta (rad) for this frame. Returns 0 for centred sticks, bad
  // dt, or non-finite input -- without changing the snap latch, so a
  // dropped frame mid-push cannot double-fire or lose a snap.
  float Feed(float stick_x, float dt) {
    if (!(dt > 0.0f) || dt > 0.25f || !std::isfinite(stick_x)) return 0.0f;
    float mag = std::fabs(stick_x);
    float sign = stick_x < 0.0f ? -1.0f : 1.0f;

    if (params_.snap_enabled) {
      if (!snap_latched_ && mag >= params_.snap_trigger) {
        snap_latched_ = true;
        return sign * params_.snap_degrees * 0.0174532925f;
      }
      if (snap_latched_ && mag < params_.snap_rearm) snap_latched_ = false;
      return 0.0f;
    }
    if (params_.smooth_enabled) {
      if (mag <= params_.deadzone) return 0.0f;
      float scaled = (mag - params_.deadzone) / (1.0f - params_.deadzone);
      if (scaled > 1.0f) scaled = 1.0f;
      return sign * scaled * params_.smooth_speed_dps * 0.0174532925f * dt;
    }
    return 0.0f;
  }

  void Reset() { snap_latched_ = false; }
  bool SnapLatched() const { return snap_latched_; }

 private:
  TurnParams params_;
  bool snap_latched_ = false;
};

}  // namespace dsvr
