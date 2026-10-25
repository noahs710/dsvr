#pragma once

#include <cmath>
#include <cstdint>

namespace dsvr {

// Physical defence bridge.
//
// Opt-in native defence-input preview, the mirror image of
// swing_attack_math.h: a raised shield hand holds native block (LB), while a
// qualified flick taps native parry (LT). The caller keeps the two channels
// mutually exclusive; neither gesture changes native combat rules. Guard windows,
// stamina, chip damage, parry judgement and ripostes stay the game's.
//
// "Wherever the shield is held": the guard is deliberately not a canonical
// pose. The caller reports only that the shield hand is up; its height,
// distance and angle are the player's choice and the block follows them. What
// makes it a guard rather than a wave is that it is held still, so the settle
// window is the whole pose test. A parry is the opposite shape: a short, sharp,
// direction-agnostic impulse that comes back to rest.
//
// Fail-closed throughout: non-finite speed, a negative speed, a non-monotonic
// clock or an impossible parameter set releases the guard and emits nothing.

struct ShieldGuardParams {
  float guard_speed_max = 0.8f;      // m/s at or below which a raised hand is still
  long long guard_settle_ms = 120;   // ms held still before the block engages
  long long release_ms = 40;         // ms the button must be up before a parry press
  long long parry_press_ms = 90;     // ms a parry press is held: registers, still reads as a tap
  long long parry_cooldown_ms = 700; // ms before the same hand may parry again
  float flick_speed_min = 2.2f;      // m/s hand speed that counts as a flick
  float flick_speed_max = 12.0f;     // m/s above which this is a swing, not a flick
  long long flick_settle_ms = 30;    // ms above flick_speed_min (kills single-frame spikes)
  long long flick_return_ms = 260;   // ms to be back at rest, or it was not a flick
};

struct ShieldGuardOutput {
  bool block = false;    // hold the native shield button
  bool parry = false;    // this frame sits inside a parry press
  bool flicked = false;  // this frame started a parry press (log once per parry)
  float peak_speed = 0.0f;
};

class ShieldGuardTrigger {
 public:
  // Speed is measured relative to the head, so walking is not a flick.
  // `raised` is the caller's head-relative height test; nothing here encodes a
  // particular guard pose.
  ShieldGuardOutput Feed(float hand_speed, bool raised, long long now_ms,
                         const ShieldGuardParams& p = {});
  void Reset() { *this = ShieldGuardTrigger{}; }
  bool Guarding() const { return blocking_; }
  bool Parrying() const { return parry_press_; }
  float Peak() const { return peak_; }

 private:
  bool primed_ = false, quiet_ = false, guarding_ = false, flicking_ = false,
       flick_raised_ = false, flick_rejected_ = false, pending_parry_ = false,
       parry_press_ = false, blocking_ = false;
  long long quiet_start_ms_ = 0, flick_start_ms_ = 0, press_start_ms_ = 0,
            block_off_ms_ = 0, last_parry_ms_ = 0, pending_expires_ms_ = 0,
            last_sample_ms_ = 0;
  float peak_ = 0.0f;
};

inline ShieldGuardOutput ShieldGuardTrigger::Feed(float hand_speed, bool raised,
                                                  long long now_ms,
                                                  const ShieldGuardParams& p) {
  ShieldGuardOutput out;
  if (!std::isfinite(hand_speed) || hand_speed < 0.0f || now_ms < 0 ||
      !std::isfinite(p.guard_speed_max) || p.guard_speed_max <= 0.0f ||
      !std::isfinite(p.flick_speed_min) || p.flick_speed_min <= p.guard_speed_max ||
      !std::isfinite(p.flick_speed_max) || p.flick_speed_max < p.flick_speed_min ||
      p.guard_settle_ms < 0 || p.release_ms <= 0 || p.parry_press_ms <= 0 ||
      p.flick_settle_ms <= 0 || p.flick_return_ms < p.flick_settle_ms ||
      p.parry_cooldown_ms < p.parry_press_ms) {
    Reset();
    return out;
  }
  if (primed_ && now_ms < last_sample_ms_) Reset();
  if (!primed_) {
    primed_ = true;
    last_sample_ms_ = now_ms;
    quiet_ = hand_speed <= p.guard_speed_max;
    quiet_start_ms_ = now_ms;
    return out;
  }
  last_sample_ms_ = now_ms;

  const bool quiet = hand_speed <= p.guard_speed_max;
  const bool fast = hand_speed >= p.flick_speed_min;

  // A flick is fast for a moment and then back to rest. Anything that stays
  // fast is a reach or a swing, and anything faster than the band is a swing
  // with the shield in hand -- neither is a parry.
  if (fast) {
    if (!flicking_) {
      flicking_ = true;
      // The shield has to be up when the flick starts, not when it ends: a
      // downward flick is a parry in a direction like any other, and the hand
      // is often still coming down when the impulse finishes.
      flick_raised_ = raised;
      flick_rejected_ = false;
      flick_start_ms_ = now_ms;
      peak_ = hand_speed;
    } else if (now_ms - flick_start_ms_ > p.flick_return_ms) {
      flick_rejected_ = true;
    } else if (hand_speed > peak_) {
      peak_ = hand_speed;
    }
  } else if (flicking_) {
    const bool long_enough = now_ms - flick_start_ms_ >= p.flick_settle_ms;
    const bool in_band = !flick_rejected_ && peak_ <= p.flick_speed_max;
    flicking_ = false;
    if (long_enough && in_band && flick_raised_ &&
        now_ms - last_parry_ms_ >= p.parry_cooldown_ms) {
      pending_parry_ = true;
      // A pending parry that never gets its released button is dropped rather
      // than queued: a stale tap arriving during a later guard is worse than
      // the parry that never happened.
      pending_expires_ms_ = now_ms + p.release_ms + p.parry_press_ms + 250;
    }
  }

  // The guard itself: up, and held still for the settle window.
  if (raised) {
    if (quiet) {
      if (!quiet_) {
        quiet_ = true;
        quiet_start_ms_ = now_ms;
      }
    } else {
      quiet_ = false;
    }
    guarding_ = quiet_ && now_ms - quiet_start_ms_ >= p.guard_settle_ms;
  } else {
    quiet_ = guarding_ = false;
  }

  // The press owns the button, and it may only start from a released one --
  // the game reads a continuous hold as a block, so a parry that never let go
  // would be a block with extra steps.
  if (parry_press_ && now_ms - press_start_ms_ >= p.parry_press_ms) {
    parry_press_ = false;
  }
  if (pending_parry_) {
    if (now_ms > pending_expires_ms_) {
      pending_parry_ = false;
    } else if (!parry_press_ && !guarding_ &&
               (block_off_ms_ == 0 || now_ms - block_off_ms_ >= p.release_ms)) {
      pending_parry_ = false;
      parry_press_ = true;
      press_start_ms_ = last_parry_ms_ = now_ms;
      out.flicked = true;
    }
  }

  out.peak_speed = peak_;
  if (parry_press_) out.parry = true;
  else if (guarding_) out.block = true;
  // The flick peak is reported on the frame that presses, then dropped: the
  // caller logs one line per parry and needs the speed that caused it.
  if (out.flicked) peak_ = 0.0f;
  if (blocking_ && !out.block) block_off_ms_ = now_ms;
  blocking_ = out.block;
  return out;
}

}  // namespace dsvr
