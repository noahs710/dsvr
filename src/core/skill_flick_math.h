#pragma once

#include <cmath>
#include <cstdint>
#include "shield_guard_math.h"

namespace dsvr {

// Skill flourish bridge: a short, sharp weapon-hand impulse that returns to
// rest taps the skill channel (LT). The game disambiguates skill vs parry by
// equipment, so the bridge stays dumb. Unlike ShieldGuardTrigger there is no
// guard state here: `raised` does not exist, and a flourish is never a block.
//
// Collision rules (enforced by the caller, not here): a firing swing and a
// thrust shape suppress the flourish instead of double-firing. Anything that
// stays fast is a reach or a swing; anything faster than the band is a swing
// with intent. Fail-closed throughout: non-finite speed, a negative speed, a
// non-monotonic clock or an impossible parameter set emits nothing and drops
// all history.
struct SkillFlickParams {
  float flick_speed_min = 2.2f;   // m/s hand speed that starts a flourish
  float flick_speed_max = 12.0f;  // m/s above which this is a swing, not a flourish
  float rest_speed_max = 0.6f;    // m/s that counts as returned to rest
  long long flick_settle_ms = 30; // ms above flick_speed_min (kills single-frame spikes)
  long long flick_return_ms = 260;// ms to be back at rest, or it was not a flourish
  long long tap_ms = 90;          // ms the skill tap is held (parry-press window)
  long long cooldown_ms = 700;    // ms before the same hand may skill again
};

struct SkillFlickOutput {
  bool tapped = false;   // this frame started a skill tap (log once per flourish)
  bool tapping = false;  // this frame sits inside a skill tap
  float peak_speed = 0.0f;
};

class SkillFlickTrigger {
 public:
  SkillFlickOutput Feed(float hand_speed, long long now_ms,
                        const SkillFlickParams& p = {});
  void Reset() { *this = SkillFlickTrigger{}; }
  bool Tapping() const { return tapping_; }

 private:
  bool primed_ = false, flicking_ = false, slowing_ = false,
       rejected_ = false, qualified_ = false, tapping_ = false,
       ever_tapped_ = false;
  long long flick_start_ms_ = 0, tap_start_ms_ = 0, last_tap_ms_ = 0,
            last_sample_ms_ = 0;
  float peak_ = 0.0f;
};

inline SkillFlickOutput SkillFlickTrigger::Feed(float hand_speed, long long now_ms,
                                              const SkillFlickParams& p) {
  SkillFlickOutput out;
  if (!std::isfinite(hand_speed) || hand_speed < 0.0f || now_ms < 0 ||
      !std::isfinite(p.flick_speed_min) || !std::isfinite(p.flick_speed_max) ||
      !std::isfinite(p.rest_speed_max) || p.flick_speed_min <= p.rest_speed_max ||
      p.flick_speed_max < p.flick_speed_min || p.flick_settle_ms <= 0 ||
      p.flick_return_ms < p.flick_settle_ms || p.tap_ms <= 0 ||
      p.cooldown_ms < p.tap_ms) {
    Reset();
    return out;
  }
  // A clock behind the last sample is a restarted timer, not a slow frame:
  // drop all history and emit nothing.
  if (primed_ && now_ms < last_sample_ms_) {
    Reset();
    return out;
  }
  primed_ = true;
  last_sample_ms_ = now_ms;

  const bool fast = hand_speed >= p.flick_speed_min;
  const bool quiet = hand_speed <= p.rest_speed_max;

  // A flourish is fast for a moment and then back to rest.
  if (fast) {
    if (!flicking_) {
      flicking_ = true;
      rejected_ = slowing_ = qualified_ = false;
      flick_start_ms_ = now_ms;
      peak_ = hand_speed;
    } else if (slowing_) {
      // Separate impulses without a return to rest are not one flourish.
      rejected_ = true;
    } else if (hand_speed > peak_) {
      peak_ = hand_speed;
    }
  } else if (flicking_) {
    if (!slowing_) {
      qualified_ = now_ms - flick_start_ms_ >= p.flick_settle_ms;
      slowing_ = true;
    }
    // Below the flick threshold can still be a moving hand. Only actual rest
    // completes the excursion.
    if (quiet) {
      const bool in_band = !rejected_ && peak_ <= p.flick_speed_max &&
          now_ms - flick_start_ms_ <= p.flick_return_ms;
      flicking_ = false;
      if (qualified_ && in_band &&
          (!ever_tapped_ || now_ms - last_tap_ms_ >= p.cooldown_ms)) {
        tapping_ = true;
        ever_tapped_ = true;
        tap_start_ms_ = last_tap_ms_ = now_ms;
        out.tapped = true;
      }
    }
  }
  if (flicking_ && now_ms - flick_start_ms_ > p.flick_return_ms)
    rejected_ = true;

  if (tapping_ && now_ms - tap_start_ms_ >= p.tap_ms) tapping_ = false;
  out.tapping = tapping_;
  out.peak_speed = peak_;
  // The peak is reported on the frame that taps, then dropped: the caller
  // logs one line per skill and needs the speed that caused it.
  if (out.tapped) peak_ = 0.0f;
  return out;
}

// Skill taps reuse the parry flick bars: a flourish is the same hand-speed
// shape on the weapon hand. No new tuning surface; the game picks skill vs
// parry by equipment.
inline SkillFlickParams SkillParamsFromGuard(const ShieldGuardParams& g) {
  SkillFlickParams p;
  p.flick_speed_min = g.flick_speed_min;
  p.flick_speed_max = g.flick_speed_max;
  p.flick_settle_ms = g.flick_settle_ms;
  p.flick_return_ms = g.flick_return_ms;
  return p;  // tap_ms / cooldown_ms keep shipped skill defaults
}

}  // namespace dsvr
