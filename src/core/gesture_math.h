#pragma once
#include <cmath>

#include "camera_math.h"

namespace dsvr {

// Legacy gesture classifier retained for regression tests/reference.
// The runtime publishes continuous motion, not these attack triggers.
// Contact-driven native combat is planned in docs/ENGINEERING.md.
//
// Conventions:
// - Positions are XR LOCAL-space metres; blade_dir is a unit vector along
//   the blade (grip -> tip) in the same space.
// - Velocity is a smoothed finite difference of hand position. XR pose
//   linear velocity, when P4 provides it, can replace the position feed
//   by feeding positions integrated from it -- do not mix both.
// - A swing fires ONCE on the rising edge (speed crossing the kind
//   threshold). It re-arms only after speed falls below half the firing
//   threshold, so one physical swing can never multi-fire.
// - PeakSpeed() is the highest smoothed speed since the last re-arm; the
//   damage scaler reads it at overlap time, not at swing end.

enum class GestureKind { None, Slash, Thrust };

struct GestureParams {
  float swing_speed_min = 1.5f;   // m/s, from [combat] swing_speed_min
  float thrust_speed_min = 1.0f;  // m/s along blade, thrust_speed_min
  float thrust_axis_cos = 0.7f;   // |dot(v_hat, blade)| above = thrust
  float rearm_fraction = 0.5f;    // re-arm below threshold * this
};

struct GestureEvent {
  GestureKind kind = GestureKind::None;
  float speed = 0.0f;      // smoothed speed at firing (m/s)
  Vec3 direction{0, 0, 0}; // unit velocity direction at firing
};

class GestureTracker {
 public:
  explicit GestureTracker(GestureParams params = {}) : params_(params) {}

  // Feed one hand sample. Returns a firing event on the rising edge only;
  // GestureKind::None otherwise. dt <= 0 or non-finite input resets the
  // velocity estimate (tracking loss) without firing.
  GestureEvent Feed(Vec3 hand_pos, Vec3 blade_dir, float dt) {
    if (!(dt > 0.0f) || dt > 0.25f || !Finite(hand_pos) ||
        !Finite(blade_dir)) {
      have_prev_ = false;
      velocity_ = {0, 0, 0};
      return {};
    }
    if (have_prev_) {
      Vec3 raw{(hand_pos.x - prev_pos_.x) / dt,
               (hand_pos.y - prev_pos_.y) / dt,
               (hand_pos.z - prev_pos_.z) / dt};
      // Tracking-loss teleport: a relocated pose in one frame is not a
      // swing (live log showed a 326811 m/s "slash" from exactly this).
      // Drop the sample and re-sync instead of firing on garbage.
      if (VLen(raw) > 25.0f) {
        prev_pos_ = hand_pos;
        velocity_ = {0, 0, 0};
        return {};
      }
      // EMA smoothing: pose jitter at 90 Hz otherwise reads as speed.
      velocity_ = VAdd(VScale(velocity_, 0.5f), VScale(raw, 0.5f));
    }
    prev_pos_ = hand_pos;
    have_prev_ = true;

    float speed = VLen(velocity_);
    if (speed > peak_speed_) peak_speed_ = speed;

    float blade_len = VLen(blade_dir);
    float axis = 0.0f;
    if (speed > 1e-4f && blade_len > 1e-4f)
      axis = std::fabs(VDot(VScale(velocity_, 1.0f / speed),
                            VScale(blade_dir, 1.0f / blade_len)));

    GestureKind kind = GestureKind::None;
    float threshold = params_.swing_speed_min;
    if (axis >= params_.thrust_axis_cos &&
        speed >= params_.thrust_speed_min) {
      kind = GestureKind::Thrust;
      threshold = params_.thrust_speed_min;
    } else if (speed >= params_.swing_speed_min) {
      kind = GestureKind::Slash;
    }

    if (armed_ && kind != GestureKind::None) {
      armed_ = false;
      active_kind_ = kind;
      return {kind, speed, speed > 1e-4f
                                 ? VScale(velocity_, 1.0f / speed)
                                 : Vec3{0, 0, 0}};
    }
    if (!armed_ && speed < threshold * params_.rearm_fraction) {
      armed_ = true;
      active_kind_ = GestureKind::None;
      peak_speed_ = speed;
    }
    return {};
  }

  void Reset() {
    have_prev_ = false;
    velocity_ = {0, 0, 0};
    peak_speed_ = 0.0f;
    armed_ = true;
    active_kind_ = GestureKind::None;
  }

  float Speed() const { return VLen(velocity_); }
  float PeakSpeed() const { return peak_speed_; }
  GestureKind ActiveKind() const { return active_kind_; }
  bool Armed() const { return armed_; }

 private:
  static bool Finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
  }

  GestureParams params_;
  Vec3 prev_pos_{0, 0, 0};
  Vec3 velocity_{0, 0, 0};
  float peak_speed_ = 0.0f;
  bool have_prev_ = false;
  bool armed_ = true;
  GestureKind active_kind_ = GestureKind::None;
};

}  // namespace dsvr
