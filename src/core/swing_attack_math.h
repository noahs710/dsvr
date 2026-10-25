#pragma once

#include <cmath>
#include <cstdint>

namespace dsvr {

// Physical attack bridge.
//
// Opt-in native attack-input preview: a qualified physical swing requests one
// ordinary native light/strong attack. The game retains its animation windows,
// stamina and hit judgement. This is not the native physical-contact submit
// owner: committed weapon collision and independent outcomes need live proof.
//
// Speed picks the weight, and a thrust earns a lower bar: a stab carries less
// tip speed than a slash but concentrates it along the blade, so the thrust
// thresholds are a fraction of the slash thresholds (same reasoning as
// CombatParams::thrust_bonus in combat_math.h).
//
// Fail-closed throughout: non-finite speed, a negative speed, a non-monotonic
// clock or an impossible parameter set resets the state and emits nothing.

struct SwingAttackParams {
  float slash_speed_min = 2.0f;   // m/s tip speed that opens a light slash
  float slash_heavy_min = 4.5f;   // m/s at/above which a slash is a strong attack
  float thrust_ratio = 0.75f;     // thrust bars as a fraction of the slash bars
  float rest_speed_max = 0.6f;    // m/s at or below which the arm counts as at rest
  long long rest_ms_min = 240;    // ms at rest required before a swing can count
  long long settle_ms = 60;       // ms the swing must last (kills single-frame spikes)
  long long hold_ms = 90;         // ms the button is held so the game sees one press
  long long cooldown_ms = 500;    // ms before this hand may attack again
};

struct SwingAttackOutput {
  bool light = false;   // hold the native light-attack button
  bool heavy = false;   // hold the native strong-attack trigger
  bool fired = false;   // this frame started an attack (log once per attack)
  float peak_speed = 0.0f;
};

class SwingAttackTrigger {
 public:
  // Speed is measured relative to the head; walking is not a swing.
  SwingAttackOutput Feed(float tip_speed, bool thrust, long long now_ms,
                         const SwingAttackParams& p = {});
  void Reset() { *this = SwingAttackTrigger{}; }
  bool Armed() const { return armed_; }
  bool Holding() const { return firing_; }
  float Peak() const { return peak_; }

 private:
  bool primed_ = false, resting_ = false, armed_ = false, swinging_ = false,
       consumed_ = false, qualifying_ = false, firing_ = false,
       heavy_ = false, ever_fired_ = false;
  long long rest_start_ms_ = 0, qualify_start_ms_ = 0, fire_ms_ = 0,
            last_fire_ms_ = 0, last_sample_ms_ = 0;
  float peak_ = 0.0f;
};

inline SwingAttackOutput SwingAttackTrigger::Feed(float tip_speed, bool thrust,
                                                  long long now_ms,
                                                  const SwingAttackParams& p) {
  SwingAttackOutput out;
  if (!std::isfinite(tip_speed) || tip_speed < 0.0f || now_ms < 0 ||
      !std::isfinite(p.slash_speed_min) || p.slash_speed_min <= 0.0f ||
      !std::isfinite(p.slash_heavy_min) || p.slash_heavy_min < p.slash_speed_min ||
      !std::isfinite(p.thrust_ratio) || p.thrust_ratio <= 0.0f ||
      p.thrust_ratio > 1.0f || !std::isfinite(p.rest_speed_max) ||
      p.rest_speed_max < 0.0f ||
      p.rest_speed_max >= p.slash_speed_min * p.thrust_ratio ||
      p.rest_ms_min <= 0 || p.settle_ms <= 0 || p.hold_ms <= 0 ||
      p.cooldown_ms < p.hold_ms) {
    Reset();
    return out;
  }
  if (primed_ && now_ms < last_sample_ms_) Reset();
  if (!primed_) {
    primed_ = true;
    resting_ = tip_speed <= p.rest_speed_max;
    rest_start_ms_ = last_sample_ms_ = now_ms;
    return out;
  }
  last_sample_ms_ = now_ms;

  const float speed_min = thrust ? p.slash_speed_min * p.thrust_ratio
                                 : p.slash_speed_min;
  const float heavy_min = thrust ? p.slash_heavy_min * p.thrust_ratio
                                 : p.slash_heavy_min;

  if (firing_ && now_ms - fire_ms_ >= p.hold_ms) firing_ = false;
  if (tip_speed <= p.rest_speed_max) {
    if (!resting_) {
      resting_ = true;
      rest_start_ms_ = now_ms;
    }
    swinging_ = qualifying_ = false;
    armed_ = now_ms - rest_start_ms_ >= p.rest_ms_min;
  } else {
    resting_ = false;
    if (!swinging_) {
      swinging_ = true;
      // Reject once at swing start: never queue movement through a cooldown.
      consumed_ = !armed_ ||
          (ever_fired_ && now_ms - last_fire_ms_ < p.cooldown_ms);
      armed_ = false;
      peak_ = tip_speed;
    } else if (tip_speed > peak_) {
      peak_ = tip_speed;
    }
    if (!consumed_ && tip_speed >= speed_min) {
      if (!qualifying_) {
        qualifying_ = true;
        qualify_start_ms_ = now_ms;
      }
      if (now_ms - qualify_start_ms_ >= p.settle_ms) {
        // A consumed swing stays consumed after the button hold expires.
        consumed_ = firing_ = ever_fired_ = true;
        heavy_ = peak_ >= heavy_min;
        fire_ms_ = last_fire_ms_ = now_ms;
        out.fired = true;
      }
    } else {
      qualifying_ = false;
    }
  }
  out.peak_speed = peak_;
  // Hold independently of subsequent motion, including an immediate stop.
  if (firing_) (heavy_ ? out.heavy : out.light) = true;
  return out;
}

}  // namespace dsvr
