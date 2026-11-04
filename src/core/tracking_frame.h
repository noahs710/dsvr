#pragma once

#include <cstdint>
#include <cmath>
#include <mutex>

#include "input.h"

namespace dsvr {

// One prediction/reference space, physical hand order (0 left, 1 right).
// This is tracking provenance, NOT proof of the pose used by a rendered image.
struct TrackingFrame {
  std::uint64_t sequence = 0;
  std::uint64_t space_epoch = 0;
  std::uint64_t captured_ms = 0;  // local monotonic clock, not XrTime
  long long predicted_time_ns = 0;
  long long display_period_ns = 0;
  bool focused = false;
  bool orientation_valid = false;
  bool position_valid = false;
  bool head_tracking_flags_available = false;
  bool head_orientation_tracked = false;
  bool head_position_tracked = false;
  float head_quat[4] = {0, 0, 0, 1};
  float head_pos[3] = {0, 0, 0};
  float vfov_rad = 0;
  float fov_aspect = 0;
  HandSample hand[2]{};
  ArmTarget arm[2]{};
};

// Render writer, arbitrary readers. No lock is held across XR/game calls.
// Read fails closed and clears output; caller supplies the same monotonic
// clock used for capture and its maximum permitted age.
class TrackingFrames {
 public:
  void Invalidate(bool new_space = false) {
    std::lock_guard<std::mutex> lock(mutex_);
    frame_ = {};
    if (new_space) ++epoch_;
  }

  std::uint64_t Publish(TrackingFrame frame) {
    for (int side = 0; side < 2; ++side) {
      if (!FinitePose(frame.hand[side].quat, frame.hand[side].pos))
        frame.hand[side].valid = false;  // aim/buttons have independent validity
      if (!FinitePose(frame.hand[side].aim_quat, frame.hand[side].aim_pos))
        frame.hand[side].aim_valid = false;
      if (!FinitePose(frame.arm[side].quat, frame.arm[side].pos))
        frame.arm[side] = {};
    }
    std::lock_guard<std::mutex> lock(mutex_);
    frame.sequence = ++sequence_;
    frame.space_epoch = epoch_;
    frame_ = frame;
    return sequence_;
  }

  std::uint64_t CurrentEpoch() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return epoch_;
  }

  bool Read(std::uint64_t now_ms, std::uint64_t max_age_ms,
            TrackingFrame& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    out = {};
    if (!frame_.sequence || !frame_.focused || !frame_.orientation_valid ||
        !frame_.position_valid || !FinitePose(frame_.head_quat, frame_.head_pos) ||
        frame_.predicted_time_ns <= 0 ||
        now_ms < frame_.captured_ms ||
        now_ms - frame_.captured_ms > max_age_ms)
      return false;
    out = frame_;
    return true;
  }

 private:
  static bool FinitePose(const float quat[4], const float pos[3]) {
    for (int i = 0; i < 3; ++i)
      if (!std::isfinite(pos[i])) return false;
    float norm2 = 0;
    for (int i = 0; i < 4; ++i) {
      if (!std::isfinite(quat[i])) return false;
      norm2 += quat[i] * quat[i];
    }
    return norm2 > 0.5f && norm2 < 1.5f;
  }

  mutable std::mutex mutex_;
  TrackingFrame frame_{};
  std::uint64_t sequence_ = 0;
  std::uint64_t epoch_ = 1;
};

}  // namespace dsvr
