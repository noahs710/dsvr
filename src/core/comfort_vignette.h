#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace dsvr {

struct ComfortMotion {
  float move = 0;          // artificial stick magnitude, before roomscale mix
  float turn_radians = 0;  // this frame's emitted artificial turn
};

// Render-thread envelope in predicted time. Physical motion alone never closes
// the aperture. No camera/projection changes, and no wall-obstruction policy.
class ComfortVignette {
 public:
  void Reset() { *this = {}; }

  float Update(ComfortMotion motion, const float head[4], std::int64_t time_ns,
               std::uint64_t epoch, bool enabled, float strength) {
    float norm = 0;
    for (unsigned i = 0; i < 4; ++i) norm += head[i] * head[i];
    if (!enabled || !epoch || time_ns <= 0 || !std::isfinite(strength) ||
        strength <= 0 || !std::isfinite(motion.move) ||
        !std::isfinite(motion.turn_radians) || !std::isfinite(norm) ||
        norm < .5f || norm > 1.5f) {
      Reset();
      return 0;
    }
    strength = std::clamp(strength, 0.f, 1.f);
    float q[4];
    for (unsigned i = 0; i < 4; ++i) q[i] = head[i] / std::sqrt(norm);
    if (epoch == epoch_ && time_ns == time_ns_) return level_ * strength;
    const float dt = static_cast<float>(time_ns - time_ns_) * 1e-9f;
    if (epoch != epoch_ || time_ns_ == 0 || dt <= 0 || dt > .25f) {
      level_ = 0;
    } else {
      float dot = 0;
      for (unsigned i = 0; i < 4; ++i) dot += q[i] * head_[i];
      // Relative quaternion vector is exactly zero for repeated poses, unlike
      // acos(dot), whose rounding near one invents a small angular velocity.
      const float x = q[0]*head_[3] - q[3]*head_[0] - q[1]*head_[2] + q[2]*head_[1];
      const float y = q[1]*head_[3] - q[3]*head_[1] - q[2]*head_[0] + q[0]*head_[2];
      const float z = q[2]*head_[3] - q[3]*head_[2] - q[0]*head_[1] + q[1]*head_[0];
      const float head_speed = 2.f * std::atan2(std::hypot(x, y, z), std::fabs(dot)) / dt;
      // ponytail: fixed 120 deg/s reference and 60/250 ms attack/release;
      // expose more tuning only after headset evidence calls for it.
      constexpr float kTurnRate = 2.0943951f;
      float target = std::clamp(std::max((motion.move - .2f) / .8f,
          std::fabs(motion.turn_radians) / (dt * kTurnRate)), 0.f, 1.f);
      if (target > 0)
        target = std::min(1.f, target + .25f * std::min(1.f, head_speed / kTurnRate));
      const float tau = target > level_ ? .06f : .25f;
      level_ += (target - level_) * (1.f - std::exp(-dt / tau));
      if (target == 0 && level_ < .0001f) level_ = 0;
    }
    for (unsigned i = 0; i < 4; ++i) head_[i] = q[i];
    time_ns_ = time_ns;
    epoch_ = epoch;
    return level_ * strength;
  }

 private:
  float level_ = 0, head_[4]{0, 0, 0, 1};
  std::int64_t time_ns_ = 0;
  std::uint64_t epoch_ = 0;
};

}  // namespace dsvr
