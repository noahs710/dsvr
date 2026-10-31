#pragma once

#include <cmath>
#include <cstdint>

namespace dsvr {

// A tracked swing requests an ordinary native attack while head-relative blade
// speed is above the stop band. Native animation and damage remain authoritative.
struct SwingAttackParams {
  float slash_speed_min = 2.0f;   // m/s that starts a light slash
  float slash_heavy_min = 4.5f;   // m/s that starts a strong slash
  float thrust_ratio = 0.75f;     // thrust start bars as a fraction of slash bars
  float rest_speed_max = 0.6f;    // m/s that ends the native attack hold
};

struct SwingAttackOutput {
  bool light = false;   // hold the native light-attack button
  bool heavy = false;   // hold the native strong-attack trigger
  bool fired = false;   // this frame started an attack (log once per swing)
  float peak_speed = 0.0f;
};

class SwingAttackTrigger {
 public:
  SwingAttackOutput Feed(float tip_speed, bool thrust, long long now_ms,
                         const SwingAttackParams& p = {});
  void Reset() { *this = SwingAttackTrigger{}; }
  bool Holding() const { return firing_; }
  float Peak() const { return peak_; }

 private:
  bool primed_ = false;
  bool start_armed_ = false;
  bool firing_ = false;
  bool heavy_ = false;
  long long last_sample_ms_ = 0;
  float peak_ = 0.0f;
};

inline SwingAttackOutput SwingAttackTrigger::Feed(
    float tip_speed, bool thrust, long long now_ms,
    const SwingAttackParams& p) {
  SwingAttackOutput out{};
  const float thrust_min = p.slash_speed_min * p.thrust_ratio;
  if (!std::isfinite(tip_speed) || tip_speed < 0.0f || now_ms < 0 ||
      !std::isfinite(p.slash_speed_min) || p.slash_speed_min <= 0.0f ||
      !std::isfinite(p.slash_heavy_min) ||
      p.slash_heavy_min < p.slash_speed_min ||
      !std::isfinite(p.thrust_ratio) || p.thrust_ratio <= 0.0f ||
      p.thrust_ratio > 1.0f || !std::isfinite(p.rest_speed_max) ||
      p.rest_speed_max < 0.0f || p.rest_speed_max >= thrust_min) {
    Reset();
    return out;
  }
  if (primed_ && now_ms < last_sample_ms_) {
    Reset();
    return out;
  }
  primed_ = true;
  last_sample_ms_ = now_ms;

  const float start_min = thrust ? thrust_min : p.slash_speed_min;
  const float heavy_min = thrust ? p.slash_heavy_min * p.thrust_ratio
                                 : p.slash_heavy_min;
  if (tip_speed <= p.rest_speed_max) {
    firing_ = false;
    heavy_ = false;
    peak_ = 0.0f;
    start_armed_ = true;
    return out;
  }
  if (!firing_ && !start_armed_) return out;
  if (!firing_ && tip_speed >= start_min) {
    firing_ = true;
    start_armed_ = false;
    heavy_ = tip_speed >= heavy_min;
    peak_ = tip_speed;
    out.fired = true;
  }
  if (firing_) {
    if (tip_speed > peak_) peak_ = tip_speed;
    out.light = !heavy_;
    out.heavy = heavy_;
  }
  out.peak_speed = peak_;
  return out;
}

}  // namespace dsvr
