#pragma once

#include <cmath>
#include <cstdint>

namespace dsvr {

// Arm-swing locomotion bridge.
//
// Opt-in native locomotion input, the movement counterpart of
// swing_attack_math.h: swinging the arms in loose alternation, the way a body
// runs, requests the native run. Nothing here moves the character -- the output
// is one forward amount that composes with the existing roomscale/stick input,
// so native acceleration, stamina drain, turn-in-place and the dash all stay
// the game's. Holding the stick forward still dashes exactly as before; this is
// an additional way in, never a replacement.
//
// What makes it a run rather than two unrelated arm movements is alternation:
// a stride counts only when the other hand was quiet when it started, and the
// pattern only counts when consecutive strides swap hands. One hand flapping is
// not a run, and neither is a two-handed swing, a stretch or a clap -- both of
// those put the hands in motion together, which is precisely what a running
// gait does not do. Speeds are head-relative, so riding a lift or walking the
// rig cannot manufacture strides.
//
// Fail-closed throughout: non-finite speeds, negative speeds, a non-monotonic
// clock or an impossible parameter set reset the gait and emit nothing.

struct ArmSwingParams {
  float stride_speed_min = 1.6f;   // m/s hand speed that counts as a stride
  float stride_speed_max = 8.0f;   // m/s above which this is a swing, not a stride
  float quiet_speed_max = 0.9f;    // m/s at or below which the other hand is quiet
  long long stride_ms_min = 60;    // ms a fast phase must last (kills spike frames)
  long long stride_ms_max = 900;   // ms after which a fast phase is a reach, not a stride
  int alternations_min = 3;        // hand swaps required before the run engages
  long long window_ms = 1400;      // ms between strides that still reads as one gait
  long long hold_ms = 600;         // ms the run lingers after the last stride
  int ramp_alternations = 6;       // swaps at which the forward amount reaches full
};

struct ArmSwingOutput {
  bool sprint = false;      // the gait is engaged: ask for the native run
  float strength = 0.0f;    // 0..1 forward amount, ramped by the stride rate
  int alternations = 0;     // completed hand swaps in the current gait
};

class ArmSwingSprint {
 public:
  // Hand speeds relative to the head, in metres per second. side 0 is the left
  // hand. Nothing here encodes a particular running pose: only the timing and
  // alternation of the two hands is examined.
  ArmSwingOutput Feed(float left_speed, float right_speed, long long now_ms,
                      const ArmSwingParams& p = {});
  void Reset() { *this = ArmSwingSprint{}; }
  bool Sprinting() const { return sprinting_; }
  int Alternations() const { return alternations_; }

 private:
  bool primed_ = false, sprinting_ = false;
  bool fast_[2] = {false, false};
  bool paired_[2] = {false, false};  // this fast phase overlapped the other hand
  bool rejected_[2] = {false, false};
  long long fast_start_ms_[2] = {0, 0};
  float fast_peak_[2] = {0.0f, 0.0f};
  int leader_ = -1;  // side of the stride that last completed
  int alternations_ = 0;
  long long last_stride_ms_ = 0, last_sample_ms_ = 0;
};

inline ArmSwingOutput ArmSwingSprint::Feed(float left_speed, float right_speed,
                                           long long now_ms,
                                           const ArmSwingParams& p) {
  ArmSwingOutput out;
  const float speed[2] = {left_speed, right_speed};
  if (!std::isfinite(speed[0]) || !std::isfinite(speed[1]) || speed[0] < 0.0f ||
      speed[1] < 0.0f || now_ms < 0 || !std::isfinite(p.stride_speed_min) ||
      p.stride_speed_min <= 0.0f || !std::isfinite(p.stride_speed_max) ||
      p.stride_speed_max <= p.stride_speed_min ||
      !std::isfinite(p.quiet_speed_max) || p.quiet_speed_max < 0.0f ||
      p.quiet_speed_max >= p.stride_speed_min || p.stride_ms_min <= 0 ||
      p.stride_ms_max < p.stride_ms_min || p.alternations_min < 2 ||
      p.window_ms <= 0 || p.hold_ms <= 0 ||
      p.ramp_alternations < p.alternations_min) {
    Reset();
    return out;
  }
  if (primed_ && now_ms < last_sample_ms_) Reset();
  if (!primed_) {
    primed_ = true;
    last_sample_ms_ = now_ms;
    for (int s = 0; s < 2; ++s) {
      fast_[s] = speed[s] >= p.stride_speed_min;
      fast_start_ms_[s] = now_ms;
      fast_peak_[s] = speed[s];
      rejected_[s] = fast_[s] && speed[s] > p.stride_speed_max;
      paired_[s] = false;
    }
    // Prime on a frame where both hands are already moving together: that is
    // not a gait either, and the first frame must not invent one.
    if (fast_[0] && fast_[1]) { paired_[0] = paired_[1] = true; }
    return out;
  }
  last_sample_ms_ = now_ms;

  for (int s = 0; s < 2; ++s) {
    const int o = 1 - s;
    const bool fast = speed[s] >= p.stride_speed_min;
    if (fast) {
      if (!fast_[s]) {
        fast_[s] = true;
        fast_start_ms_[s] = now_ms;
        fast_peak_[s] = speed[s];
        rejected_[s] = speed[s] > p.stride_speed_max;
        // The partner has to be quiet when the stride starts, exactly as a
        // running arm is quiet at the turnaround of its swing. The test is
        // taken at the start and never at the end, so a crossover inside one
        // frame cannot cancel a stride that was already under way.
        paired_[s] = fast_[o];
        if (fast_[o]) paired_[o] = true;  // and it cancels the phase already open
      } else {
        if (now_ms - fast_start_ms_[s] > p.stride_ms_max) rejected_[s] = true;
        if (speed[s] > fast_peak_[s]) fast_peak_[s] = speed[s];
      }
      continue;
    }
    if (!fast_[s]) continue;
    // Fast phase finished: decide whether it was a stride at all.
    const bool long_enough = now_ms - fast_start_ms_[s] >= p.stride_ms_min;
    const bool in_band = !rejected_[s] && fast_peak_[s] <= p.stride_speed_max;
    fast_[s] = false;
    if (!long_enough || !in_band || paired_[s]) continue;
    if (leader_ >= 0 && leader_ != s && now_ms - last_stride_ms_ <= p.window_ms) {
      ++alternations_;
    } else {
      // Same hand twice, or a gap longer than one gait: the pattern restarted,
      // so the count restarts with it rather than carrying stale strides.
      alternations_ = 0;
    }
    leader_ = s;
    last_stride_ms_ = now_ms;
  }

  if (alternations_ >= p.alternations_min &&
      now_ms - last_stride_ms_ <= p.hold_ms) {
    sprinting_ = true;
  } else if (now_ms - last_stride_ms_ > p.hold_ms) {
    sprinting_ = false;
    alternations_ = 0;
  }

  out.alternations = alternations_;
  if (sprinting_) {
    const int span = p.ramp_alternations - p.alternations_min + 1;
    const int over = alternations_ - p.alternations_min + 1;
    float strength = static_cast<float>(over) / static_cast<float>(span);
    if (!(strength > 0.0f)) strength = 0.0f;
    if (strength > 1.0f) strength = 1.0f;
    out.strength = strength;
    out.sprint = true;
  }
  return out;
}

}  // namespace dsvr
