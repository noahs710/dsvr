#include "../src/core/swing_attack_math.h"

#include <cmath>
#include <cstdio>

int main() {
  int failures = 0;
  auto check = [&](bool ok, const char* what) {
    if (!ok) {
      std::printf("FAIL: %s\n", what);
      ++failures;
    }
  };
  using namespace dsvr;
  const SwingAttackParams params{};

  // The first threshold crossing requests the native attack immediately. Keep
  // holding it through the physical swing and release only in the stop-speed band.
  {
    SwingAttackTrigger trigger;
    trigger.Feed(0.0f, false, 0, params);  // one stop-band sample arms the motion edge
    const auto start = trigger.Feed(params.slash_speed_min, false, 11, params);
    check(start.fired && start.light && !start.heavy,
          "slash threshold starts the native attack on this sample");
    const auto moving = trigger.Feed(1.2f, false, 20, params);
    check(moving.light && !moving.fired,
          "native attack stays held while sword speed exceeds the stop threshold");
    const auto stopped = trigger.Feed(params.rest_speed_max, false, 30, params);
    check(!stopped.light && !stopped.heavy && !trigger.Holding(),
          "motion slowing into the stop band ends the attack hold");
    const auto next = trigger.Feed(params.slash_speed_min, false, 31, params);
    check(next.fired && next.light,
          "a following swing starts immediately without a timed cooldown");
  }

  // Attack weight is selected at animation start, while the native animation
  // and collision system retains its normal timing and damage rules.
  {
    SwingAttackTrigger trigger;
    trigger.Feed(0.0f, false, 0, params);
    const auto strong = trigger.Feed(params.slash_heavy_min, false, 11, params);
    check(strong.fired && strong.heavy && !strong.light,
          "strong threshold starts the native strong attack immediately");
    const auto follow_through = trigger.Feed(3.0f, false, 1000, params);
    check(follow_through.heavy && !follow_through.fired,
          "follow-through keeps one native attack held without repeating it");
  }

  // Thrust bars are lower, but the same stop-speed end condition applies.
  {
    SwingAttackTrigger trigger;
    trigger.Feed(0.0f, true, 0, params);
    const auto thrust = trigger.Feed(params.slash_speed_min * params.thrust_ratio,
                                     true, 11, params);
    check(thrust.fired && thrust.light,
          "a thrust starts at its lower speed threshold");
    const auto end = trigger.Feed(params.rest_speed_max, true, 11, params);
    check(!end.light && !end.heavy,
          "a thrust also ends when the tracked motion slows");
  }

  // Speeds between start and stop bands neither start nor end a swing.
  {
    SwingAttackTrigger trigger;
    check(!trigger.Feed(2.1f, false, 0, params).fired,
          "startup mid-swing waits for an observed stop-speed sample");
    trigger.Feed(0.0f, false, 1, params);
    check(!trigger.Feed(1.0f, false, 2, params).fired,
          "sub-threshold motion does not start an attack");
    check(trigger.Feed(2.0f, false, 11, params).light,
          "threshold crossing starts immediately after the stop sample");
    check(trigger.Feed(1.0f, false, 22, params).light,
          "the hysteresis band keeps an active swing held");
    check(!trigger.Feed(0.5f, false, 33, params).light,
          "the stop band releases an active swing");
  }

  // Invalid tracking/parameters fail closed; a subsequent valid sample can start.
  {
    SwingAttackTrigger trigger;
    check(!trigger.Feed(NAN, false, 0, params).fired,
          "non-finite speed is refused");
    trigger.Feed(0.0f, false, 1, params);
    check(trigger.Feed(2.1f, false, 11, params).fired,
          "valid speed starts immediately after recovery");
    auto invalid = params;
    invalid.rest_speed_max = invalid.slash_speed_min;
    check(!trigger.Feed(3.0f, false, 22, invalid).fired && !trigger.Holding(),
          "overlapping start and stop thresholds fail closed");
  }

  // Backwards timestamps discard the current sample rather than queueing an attack.
  {
    SwingAttackTrigger trigger;
    trigger.Feed(0.0f, false, 100, params);
    check(trigger.Feed(2.1f, false, 111, params).fired,
            "clock test starts an attack");
    const auto reversed = trigger.Feed(3.0f, false, 99, params);
    check(!reversed.fired && !reversed.light && !trigger.Holding(),
          "backwards sample clock cancels the active hold");
    trigger.Feed(0.0f, false, 100, params);
    check(trigger.Feed(2.1f, false, 111, params).fired,
          "the next monotonic sample can start again immediately");
  }

  if (failures) return 1;
  std::printf("swing_attack tests passed\n");
  return 0;
}
