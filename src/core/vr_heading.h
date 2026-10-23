#pragma once

#include <cmath>

#include "camera_math.h"

namespace dsvr {

// Turn-in-place comfort window: head yaw within +-deadzone leaves the torso
// alone; sustained yaw beyond it drags the body toward the head in bounded
// steps (at most max_step per call), keeping a residual bend so the
// shoulders never snap to the gaze.
inline constexpr float kTorsoFollowDeadzoneRad = 0.5f;
inline constexpr float kTorsoFollowMaxStepRad = 0.15f;

inline float TorsoFollowDelta(float head_yaw_rad,
                              float deadzone_rad = kTorsoFollowDeadzoneRad,
                              float max_step_rad = kTorsoFollowMaxStepRad) {
  if (!std::isfinite(head_yaw_rad) || !std::isfinite(deadzone_rad) ||
      !std::isfinite(max_step_rad))
    return 0.0f;
  if (deadzone_rad < 0.0f || max_step_rad <= 0.0f) return 0.0f;
  const float magnitude = std::fabs(head_yaw_rad);
  if (magnitude <= deadzone_rad) return 0.0f;
  const float excess = magnitude - deadzone_rad;
  const float step = excess < max_step_rad ? excess : max_step_rad;
  return head_yaw_rad > 0.0f ? step : -step;
}

// Frame-rate independent follow: the per-call cap above is a per-second
// rate in disguise, so at low frame rates the body drags at the same
// speed but in coarse steps the user feels as spinning. Scale the step
// by elapsed time (1.5 rad/s), clamped to the legacy per-call cap so
// high-rate behaviour is unchanged. dt<=0 or non-finite yields no step.
inline float TorsoFollowDeltaDt(float head_yaw_rad, float dt_seconds,
                                float deadzone_rad = kTorsoFollowDeadzoneRad,
                                float rate_rad_s = 1.5f) {
  if (!std::isfinite(dt_seconds) || dt_seconds <= 0.0f ||
      !std::isfinite(rate_rad_s) || rate_rad_s <= 0.0f)
    return 0.0f;
  const float step = rate_rad_s * dt_seconds;
  return TorsoFollowDelta(head_yaw_rad, deadzone_rad,
                          step < kTorsoFollowMaxStepRad ? step
                                                        : kTorsoFollowMaxStepRad);
}

struct VrLocomotionCandidate {
  float stick_x = 0.0f;
  float stick_y = 0.0f;
  bool valid = false;
};

class VrHeading {
 public:
  bool IsReady() const { return ready_; }
  bool ApplyTurn(float delta_rad) {
    if (!ready_ || !std::isfinite(delta_rad)) return false;
    // Reduce the delta before addition so a very large finite input cannot
    // erase the existing heading. Keep the accumulator bounded and double.
    constexpr double kTau = 6.283185307179586476925286766559;
    yaw_ = std::remainder(yaw_ + std::remainder(delta_rad, kTau), kTau);
    return true;
  }
  void Reset() {
    ready_ = false;
    yaw_ = 0.0;
  }
  bool SeedOnce(Vec3 native_forward) {
    return !ready_ && Recenter(native_forward);
  }
  bool Recenter(Vec3 native_forward) {
    if (!std::isfinite(native_forward.x) ||
        !std::isfinite(native_forward.y) ||
        !std::isfinite(native_forward.z) ||
        std::hypot(native_forward.x, native_forward.z) < 1e-4f) return false;
    yaw_ = std::atan2(static_cast<double>(native_forward.x),
                      static_cast<double>(native_forward.z));
    ready_ = true;
    return true;
  }
  Basis BodyBasis() const {
    const float s = static_cast<float>(std::sin(yaw_));
    const float c = static_cast<float>(std::cos(yaw_));
    return {{c, 0, -s}, {0, 1, 0}, {s, 0, c}};
  }
  VrLocomotionCandidate CandidateLocomotion(float stick_x, float stick_y,
                                              Vec3 movement_basis_forward) const {
    if (!ready_ || !std::isfinite(stick_x) || !std::isfinite(stick_y)) return {};
    VrHeading native;
    if (!native.SeedOnce(movement_basis_forward)) return {};
    const Basis own = BodyBasis(), movement = native.BodyBasis();
    const Vec3 desired = VAdd(VScale(own.r, stick_x), VScale(own.f, stick_y));
    float x = VDot(desired, movement.r);
    float y = VDot(desired, movement.f);
    const float magnitude = std::hypot(x, y);
    if (!std::isfinite(magnitude)) return {};
    if (magnitude > 1.0f) { x /= magnitude; y /= magnitude; }
    return {x, y, true};
  }

 private:
  double yaw_ = 0.0;
  bool ready_ = false;
};

}  // namespace dsvr
