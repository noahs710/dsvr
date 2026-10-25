#pragma once

#include <cmath>
#include <cstdint>

namespace dsvr {

// Reach-to-interact bridge.
//
// Thrusting one hand forward through the space in front of the head taps the
// native interact button. No native prompt flag is bound, so the gesture alone
// decides when the button is pressed -- that is the operator's chosen trade-off
// and the reason this stays opt-in: reaching toward a fog gate, a door or an
// enemy's face can interact. Nothing here opens a menu, picks up an item or
// decides what the target was; it requests one ordinary native button tap and
// the game's own interaction rules judge the rest.
//
// What separates a deliberate reach from a hand that merely happens to be out
// front: both hands rest first, the motion points forward rather than simply
// being fast, the hand actually arrives inside the reach envelope, and the
// other hand stays still throughout. After one tap the hand must return to rest
// before another can fire, so waving at the world cannot repeat-interact.
//
// Fail-closed throughout: non-finite or negative inputs, a clock that steps
// backwards, or an impossible parameter set reset the detector and emit
// nothing.

struct ReachIntentParams {
  float forward_min_m = 0.75f;   // m ahead of the head the hand must arrive at
  float forward_max_m = 2.2f;    // m ahead of the head beyond which nothing counts
  float speed_min = 1.2f;        // m/s forward component that reads as a thrust
  float dominance_min = 0.6f;    // fraction of hand speed that must point forward
  float quiet_max = 0.8f;        // m/s at or below which a hand counts as still
  long long settle_ms = 250;     // ms both hands must be still before a thrust counts
  long long press_ms = 60;       // ms the caller holds the interact tap down
  long long cooldown_ms = 900;   // ms before the same hand may reach again
};

struct ReachIntentOutput {
  bool pressed = false;    // tap the native interact button
  long long press_ms = 0;  // how long the caller holds it down
};

class ReachIntentTrigger {
 public:
  // forward_offset: metres the hand sits ahead of the head along the head's own
  // facing direction (negative = behind the head). forward_speed / speed: the
  // head-relative hand velocity's forward component and magnitude in m/s --
  // head-relative so walking the room or riding a lift cannot thrust a hand
  // into the envelope by moving the whole body. other_speed: magnitude of the
  // opposite hand's head-relative velocity.
  ReachIntentOutput Feed(float forward_offset, float forward_speed, float speed,
                         float other_speed, long long now_ms,
                         const ReachIntentParams& p = {});
  void Reset() { *this = ReachIntentTrigger{}; }
  bool Armed() const { return armed_; }

 private:
  bool primed_ = false, armed_ = true, quiet_run_ = false;
  long long quiet_since_ms_ = 0, last_fire_ms_ = 0, last_sample_ms_ = 0;
};

inline ReachIntentOutput ReachIntentTrigger::Feed(float forward_offset,
                                                  float forward_speed,
                                                  float speed, float other_speed,
                                                  long long now_ms,
                                                  const ReachIntentParams& p) {
  ReachIntentOutput out;
  if (!std::isfinite(forward_offset) || !std::isfinite(forward_speed) ||
      !std::isfinite(speed) || !std::isfinite(other_speed) || speed < 0.0f ||
      other_speed < 0.0f || now_ms < 0 || !std::isfinite(p.forward_min_m) ||
      p.forward_min_m <= 0.0f || !std::isfinite(p.forward_max_m) ||
      p.forward_max_m <= p.forward_min_m || !std::isfinite(p.speed_min) ||
      p.speed_min <= 0.0f || !std::isfinite(p.dominance_min) ||
      p.dominance_min <= 0.0f || p.dominance_min > 1.0f ||
      !std::isfinite(p.quiet_max) || p.quiet_max <= 0.0f ||
      p.quiet_max >= p.speed_min || p.settle_ms < 0 || p.press_ms <= 0 ||
      p.cooldown_ms < p.press_ms * 2) {
    Reset();
    return out;
  }
  // A clock that steps behind the last sample (restarted timer, recentered
  // session) invalidates the history rather than firing on a time warp.
  if (primed_ && now_ms < last_sample_ms_) {
    Reset();
    return out;
  }
  const bool quiet = speed <= p.quiet_max;
  const bool other_quiet = other_speed <= p.quiet_max;
  if (!primed_) {
    primed_ = true;
    last_sample_ms_ = now_ms;
    quiet_since_ms_ = now_ms;
    quiet_run_ = quiet && other_quiet;
    // The first frame only establishes whether the hands are at rest; a hand
    // that is already flying forward when tracking starts is not a reach.
    armed_ = quiet_run_;
    return out;
  }
  last_sample_ms_ = now_ms;
  if (quiet && other_quiet) {
    // The stamp marks the START of the rest run, not the last quiet frame: the
    // settle test measures how long the hands had been still before this
    // moving frame, and a per-frame stamp would always read one frame.
    if (!quiet_run_) quiet_since_ms_ = now_ms;
    quiet_run_ = true;
    armed_ = true;
    return out;
  }
  quiet_run_ = false;
  // Moving. A thrust counts only if the hands had been still for settle_ms
  // first, the other hand stayed still throughout, and the motion is forward
  // rather than merely fast.
  if (!armed_ || now_ms - quiet_since_ms_ < p.settle_ms || !other_quiet) return out;
  if (speed < p.speed_min || forward_speed < p.speed_min) return out;
  if (forward_speed < speed * p.dominance_min) return out;
  if (forward_offset < p.forward_min_m || forward_offset > p.forward_max_m) return out;
  if (last_fire_ms_ && now_ms - last_fire_ms_ < p.cooldown_ms) return out;
  last_fire_ms_ = now_ms;
  armed_ = false;  // the hand must come back to rest before the next tap
  out.pressed = true;
  out.press_ms = p.press_ms;
  return out;
}

}  // namespace dsvr
