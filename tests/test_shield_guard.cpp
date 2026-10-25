#include "../src/core/shield_guard_math.h"

#include <cmath>
#include <cstdio>

// The physical defence bridge: a raised shield hand blocks wherever it is held,
// a flick of that hand is one parry tap, and neither survives bad input.
int main() {
  int failures = 0;
  auto check = [&](bool ok, const char* what) {
    if (!ok) {
      std::printf("FAIL: %s\n", what);
      ++failures;
    }
  };
  using namespace dsvr;

  ShieldGuardParams p;  // shipped defaults
  long long t = 0;
  auto frame = [&](ShieldGuardTrigger& tr, float speed, bool raised) {
    t += 11;  // 90 Hz
    return tr.Feed(speed, raised, t, p);
  };
  struct Run {
    int parries = 0;
    bool blocked = false, parried = false;
  };
  auto hold = [&](ShieldGuardTrigger& tr, float speed, bool raised, int ms,
                  Run& run) {
    ShieldGuardOutput last{};
    for (int elapsed = 0; elapsed < ms; elapsed += 11) {
      last = frame(tr, speed, raised);
      if (last.flicked) ++run.parries;
      if (last.block) run.blocked = true;
      if (last.parry) run.parried = true;
    }
    return last;
  };

  // A guard is a raised hand held still: the settle window is the pose test,
  // and nothing about the pose itself (height, distance, angle) is required.
  {
    ShieldGuardTrigger tr;
    long long first_block_ms = -1;
    for (int elapsed = 0; elapsed < 220; elapsed += 11) {
      if (frame(tr, 0.1f, true).block && first_block_ms < 0) first_block_ms = t;
    }
    check(first_block_ms >= p.guard_settle_ms && tr.Guarding(),
          "a raised, still hand becomes a block only after the settle window");
    ShieldGuardTrigger moving;
    Run quiet_run;
    hold(moving, 1.5f, true, 400, quiet_run);
    check(!quiet_run.blocked && !moving.Guarding(),
          "a raised hand that never holds still is not a guard");
    ShieldGuardTrigger low;
    Run low_run;
    hold(low, 0.1f, false, 400, low_run);
    check(!low_run.blocked, "a hand that is not up never blocks");
  }
  // Releasing the guard is immediate: the block follows the hand down.
  {
    ShieldGuardTrigger tr;
    Run run;
    hold(tr, 0.1f, true, 400, run);
    check(run.blocked, "guard established");
    const auto down = frame(tr, 0.1f, false);
    check(!down.block && !tr.Guarding(), "lowering the shield drops the block at once");
    const auto up = frame(tr, 0.1f, true);
    check(!up.block, "the block re-settles rather than snapping back on");
  }
  // A flick is a tap of the same button, and it may only start from a released
  // button: the game separates a block from a parry by press length alone.
  {
    ShieldGuardTrigger tr;
    Run run;
    hold(tr, 0.1f, true, 400, run);
    hold(tr, 5.0f, true, 66, run);  // fast in one direction, any direction
    const auto back = hold(tr, 0.1f, true, 400, run);
    check(run.parries == 1, "one flick is one parry press");
    check(run.parried && tr.Peak() == 0.0f, "the parry press was emitted");
    check(back.block, "the guard resumes after the parry press");
    check(!tr.Parrying(), "the parry press ends on its own deadline");
  }
  // The press is not truncated by what the hand does next, and it does not
  // overlap the block: the button has to be up first.
  {
    ShieldGuardTrigger tr;
    Run run;
    hold(tr, 0.1f, true, 300, run);
    hold(tr, 4.0f, true, 55, run);
    int parry_frames = 0, block_frames = 0;
    for (int elapsed = 0; elapsed < 400; elapsed += 11) {
      const auto out = frame(tr, 0.1f, false);  // hand dropped mid-parry
      if (out.flicked) ++run.parries;
      if (out.parry) ++parry_frames;
      if (out.block) ++block_frames;
      check(!(out.parry && out.block), "block and parry never share a frame");
    }
    check(run.parries == 1, "a flick still parries when the hand keeps moving down");
    check(parry_frames * 11 >= p.parry_press_ms && parry_frames * 11 <
              p.parry_press_ms + 22,
          "the parry press is held for its full window, no longer");
    check(block_frames == 0, "a dropped hand does not resume the block");
  }
  // A second flick inside the parry cooldown is not a second parry.
  {
    ShieldGuardTrigger tr;
    Run run;
    hold(tr, 0.1f, true, 300, run);
    hold(tr, 5.0f, true, 66, run);
    hold(tr, 0.1f, true, 400, run);
    hold(tr, 5.0f, true, 66, run);
    hold(tr, 0.1f, true, 400, run);
    check(run.parries == 1, "the parry cooldown rejects the next flick");
  }
  // Shape refusals: a long fast movement is a reach, an extreme speed is a
  // swing, a single frame is noise, and a flick with the shield down is nothing.
  {
    ShieldGuardTrigger long_move;
    Run run;
    hold(long_move, 0.1f, true, 300, run);
    hold(long_move, 4.0f, true, 400, run);
    hold(long_move, 0.1f, true, 400, run);
    check(run.parries == 0, "a movement that stays fast is not a flick");

    ShieldGuardTrigger too_fast;
    run = Run{};
    hold(too_fast, 0.1f, true, 300, run);
    hold(too_fast, 20.0f, true, 66, run);
    hold(too_fast, 0.1f, true, 400, run);
    check(run.parries == 0, "a swing with the shield in hand is not a parry");

    ShieldGuardTrigger spike;
    run = Run{};
    hold(spike, 0.1f, true, 300, run);
    frame(spike, 9.0f, true);
    hold(spike, 0.1f, true, 400, run);
    check(run.parries == 0, "one fast frame does not parry");

    ShieldGuardTrigger stowed;
    run = Run{};
    hold(stowed, 0.1f, false, 300, run);
    hold(stowed, 5.0f, false, 66, run);
    hold(stowed, 0.1f, false, 400, run);
    check(run.parries == 0, "a flick with the shield down is not a parry");
  }
  // A guard that never released cannot parry: the pending tap expires instead
  // of arriving as a block with a press welded onto it.
  {
    ShieldGuardParams tight = p;
    tight.guard_settle_ms = 0;  // the guard re-engages the instant the hand stops
    ShieldGuardTrigger tr;
    long long local = 0;
    auto step = [&](float speed, bool raised) {
      local += 11;
      return tr.Feed(speed, raised, local, tight);
    };
    int parries = 0;
    bool blocked = false;
    for (int i = 0; i < 30; ++i) blocked |= step(0.1f, true).block;
    for (int i = 0; i < 6; ++i) step(5.0f, true);
    for (int i = 0; i < 120; ++i) {
      const auto out = step(0.1f, true);
      if (out.flicked) ++parries;
      blocked |= out.block;
    }
    check(parries == 0 && blocked,
          "a guard that never let go expires the pending parry instead of pressing");
  }
  // Fail-closed: bad input releases the guard rather than leaving a held block.
  {
    ShieldGuardTrigger tr;
    Run run;
    hold(tr, 0.1f, true, 300, run);
    check(run.blocked, "guard established before the fault");
    const auto nan = frame(tr, NAN, true);
    check(!nan.block && !nan.parry && !tr.Guarding() && !tr.Parrying(),
          "a non-finite speed drops the guard");
    hold(tr, 0.1f, true, 300, run);
    const auto negative = frame(tr, -1.0f, true);
    check(!negative.block, "a negative speed drops the guard");

    ShieldGuardTrigger clock;
    run = Run{};
    hold(clock, 0.1f, true, 300, run);
    long long saved = t;
    t -= 5000;  // a restarted timer
    const auto back = frame(clock, 0.1f, true);
    t = saved;
    check(!back.block && !clock.Guarding(),
          "a clock behind the last sample resets rather than replaying a guard");

    ShieldGuardParams inverted;
    inverted.flick_speed_min = inverted.guard_speed_max;  // no band between them
    ShieldGuardTrigger bad;
    run = Run{};
    long long local = 0;
    for (int i = 0; i < 100; ++i) {
      local += 11;
      const auto out = bad.Feed(i % 2 ? 0.1f : 5.0f, true, local, inverted);
      if (out.flicked) ++run.parries;
      if (out.block || out.parry) run.blocked = true;
    }
    check(!run.blocked && run.parries == 0, "an inverted speed band emits nothing");

    ShieldGuardParams short_press = p;
    short_press.parry_cooldown_ms = short_press.parry_press_ms - 1;
    ShieldGuardTrigger bad2;
    const auto out = bad2.Feed(0.1f, true, t += 11, short_press);
    check(!out.block, "a cooldown shorter than the press is refused");
  }
  // Identical inputs reproduce identical output: the guard is a policy, not a
  // running total.
  {
    ShieldGuardTrigger a, b;
    Run ra, rb;
    hold(a, 0.1f, true, 300, ra);
    hold(b, 0.1f, true, 300, rb);
    hold(a, 5.0f, true, 66, ra);
    hold(b, 5.0f, true, 66, rb);
    hold(a, 0.1f, true, 500, ra);
    hold(b, 0.1f, true, 500, rb);
    check(ra.parries == rb.parries && ra.blocked == rb.blocked &&
              ra.parried == rb.parried,
          "the same motion reports the same defence twice over");
  }

  if (failures) {
    std::printf("%d shield_guard test(s) failed\n", failures);
    return 1;
  }
  std::puts("shield_guard tests passed");
  return 0;
}
