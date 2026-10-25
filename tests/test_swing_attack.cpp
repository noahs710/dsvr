#include "../src/core/swing_attack_math.h"

#include <cmath>
#include <cstdio>

// The physical attack bridge: a swing only attacks when it starts from a real
// backswing, one swing is one attack, and speed plus thrust/slash decide
// whether the game sees a light or a strong attack.
int main() {
  int failures = 0;
  auto check = [&](bool ok, const char* what) {
    if (!ok) {
      std::printf("FAIL: %s\n", what);
      ++failures;
    }
  };
  using namespace dsvr;

  SwingAttackParams p;  // shipped defaults
  long long t = 0;
  auto frame = [&](SwingAttackTrigger& tr, float speed, bool thrust) {
    t += 11;  // 90 Hz
    return tr.Feed(speed, thrust, t, p);
  };
  auto hold = [&](SwingAttackTrigger& tr, float speed, bool thrust, int ms,
                  int& fires, bool& light_any, bool& heavy_any) {
    SwingAttackOutput last{};
    for (int elapsed = 0; elapsed < ms; elapsed += 11) {
      last = frame(tr, speed, thrust);
      if (last.fired) ++fires;
      if (last.light) light_any = true;
      if (last.heavy) heavy_any = true;
    }
    return last;
  };
  auto rest = [&](SwingAttackTrigger& tr, int ms) {
    int fires = 0;
    bool light = false, heavy = false;
    return hold(tr, 0.0f, false, ms, fires, light, heavy), fires;
  };

  // Waving without ever lowering the arm is not an attack.
  {
    SwingAttackTrigger tr;
    int fires = 0;
    bool light = false, heavy = false;
    hold(tr, 6.0f, false, 900, fires, light, heavy);
    check(fires == 0 && !light && !heavy, "a continuous wave never attacks");
    check(!tr.Armed(), "an arm that never rests is never armed");
  }

  // Backswing then a moderate slash: exactly one light attack.
  {
    SwingAttackTrigger tr;
    rest(tr, 320);
    check(tr.Armed(), "resting the arm arms the next swing");
    int fires = 0;
    bool light = false, heavy = false;
    hold(tr, 3.0f, false, 220, fires, light, heavy);
    check(fires == 1 && light && !heavy, "a moderate slash is one light attack");
  }

  // A fast slash is a strong attack.
  {
    SwingAttackTrigger tr;
    rest(tr, 320);
    int fires = 0;
    bool light = false, heavy = false;
    hold(tr, 6.5f, false, 220, fires, light, heavy);
    check(fires == 1 && heavy && !light, "a fast slash is a strong attack");
  }

  // Thrusts qualify lower, both for light and for strong.
  {
    SwingAttackTrigger tr;
    rest(tr, 320);
    int fires = 0;
    bool light = false, heavy = false;
    hold(tr, 1.7f, true, 220, fires, light, heavy);
    check(fires == 1 && light, "a slow thrust attacks where a slow slash would not");

    SwingAttackTrigger slow;
    rest(slow, 320);
    fires = 0;
    light = heavy = false;
    hold(slow, 1.7f, false, 220, fires, light, heavy);
    check(fires == 0, "the same speed as a slash stays below the slash bar");

    SwingAttackTrigger stab;
    rest(stab, 320);
    fires = 0;
    light = heavy = false;
    hold(stab, 3.6f, true, 220, fires, light, heavy);
    check(fires == 1 && heavy, "a thrust reaches strong at its lower bar");
  }

  // A single-frame spike (tracking jitter, teleport) is not a swing.
  {
    SwingAttackTrigger tr;
    rest(tr, 320);
    int fires = 0;
    bool light = false, heavy = false;
    frame(tr, 9.0f, false);
    rest(tr, 320);
    hold(tr, 9.0f, false, 11, fires, light, heavy);
    check(fires == 0 && !light && !heavy, "one spike frame does not attack");
  }

  // One swing, one attack: follow-through and flailing cannot machine-gun.
  {
    SwingAttackTrigger tr;
    rest(tr, 320);
    int fires = 0;
    bool light = false, heavy = false;
    hold(tr, 6.0f, false, 1200, fires, light, heavy);
    check(fires == 1 && heavy && !light,
          "one swing is one attack however long it lasts");
  }

  // The press is discrete: held long enough to register, then released even
  // though the arm is still moving.
  {
    SwingAttackTrigger tr;
    rest(tr, 320);
    int fires = 0;
    SwingAttackOutput out{};
    bool held_after_release = false;
    for (int elapsed = 0; elapsed < 400; elapsed += 11) {
      out = frame(tr, 3.0f, false);
      if (out.fired) ++fires;
      if (fires && elapsed > 200 && out.light) held_after_release = true;
    }
    check(fires == 1 && !held_after_release && !out.light,
          "the attack press releases while the arm is still moving");
  }

  // Cooldown: a second swing needs both a fresh backswing and the interval.
  {
    SwingAttackTrigger tr;
    rest(tr, 320);
    int fires = 0;
    bool light = false, heavy = false;
    hold(tr, 3.0f, false, 220, fires, light, heavy);
    check(fires == 1, "first swing attacks");
    rest(tr, 300);
    light = heavy = false;
    hold(tr, 3.0f, false, 220, fires, light, heavy);
    check(fires == 1, "a second swing inside the cooldown does not attack");
    rest(tr, 400);
    light = heavy = false;
    hold(tr, 3.0f, false, 220, fires, light, heavy);
    check(fires == 2, "after the cooldown a fresh backswing attacks again");
  }

  // Fail closed: non-finite speed drops the swing history, and the next real
  // backswing still works.
  {
    SwingAttackTrigger tr;
    rest(tr, 320);
    frame(tr, NAN, false);
    int fires = 0;
    bool light = false, heavy = false;
    hold(tr, 3.0f, false, 220, fires, light, heavy);
    check(fires == 0, "a NaN speed discards the armed backswing");
    rest(tr, 320);
    light = heavy = false;
    hold(tr, 3.0f, false, 220, fires, light, heavy);
    check(fires == 1, "the bridge recovers after invalid tracking");
  }

  // A clock that steps behind the last attack (restarted timer) must not fire.
  {
    SwingAttackTrigger tr;
    rest(tr, 320);
    int fires = 0;
    bool light = false, heavy = false;
    hold(tr, 3.0f, false, 220, fires, light, heavy);
    check(fires == 1, "the clock test starts from one real attack");
    const long long saved = t;
    t -= 4000;
    light = heavy = false;
    hold(tr, 6.0f, false, 220, fires, light, heavy);
    check(fires == 1, "a backwards clock never attacks");
    t = saved + 1000;
    rest(tr, 320);
    light = heavy = false;
    hold(tr, 3.0f, false, 220, fires, light, heavy);
    check(fires == 2, "the bridge re-arms after a clock reset");
  }

  // Impossible parameters attack nobody.
  {
    SwingAttackParams bad;
    bad.slash_heavy_min = 0.5f;  // below the light bar
    SwingAttackTrigger tr;
    long long local = 0;
    int fires = 0;
    for (int i = 0; i < 100; ++i) {
      local += 11;
      if (tr.Feed(i < 20 ? 0.0f : 7.0f, false, local, bad).fired) ++fires;
    }
    check(fires == 0, "an inverted speed band never attacks");
  }

  // Stopping immediately after firing must not truncate the native press.
  {
    SwingAttackTrigger tr;
    tr.Feed(0, false, 0, p);
    tr.Feed(0, false, 250, p);
    tr.Feed(3, false, 260, p);
    const auto fired = tr.Feed(3, false, 330, p);
    const auto stopped = tr.Feed(0, false, 341, p);
    const auto released = tr.Feed(0, false, 430, p);
    check(fired.fired && stopped.light && tr.Holding() == false &&
          !released.light && !released.heavy,
          "resting does not truncate the press or keep it held past its deadline");
  }
  // A rejected swing is consumed, not queued until its cooldown expires.
  {
    SwingAttackTrigger tr;
    tr.Feed(0, false, 0, p);
    tr.Feed(0, false, 250, p);
    tr.Feed(3, false, 260, p);
    check(tr.Feed(3, false, 330, p).fired, "cooldown probe fires once");
    tr.Feed(0, false, 400, p);
    tr.Feed(0, false, 650, p);
    int fires = 0;
    for (long long ms = 660; ms <= 1100; ms += 11)
      fires += tr.Feed(3, false, ms, p).fired;
    check(fires == 0, "a swing begun during cooldown cannot attack later");
  }
  // A spike followed by sub-threshold movement is not sustained attack energy.
  {
    SwingAttackTrigger tr;
    tr.Feed(0, false, 0, p);
    tr.Feed(0, false, 250, p);
    tr.Feed(9, false, 260, p);
    int fires = 0;
    for (long long ms = 271; ms < 500; ms += 11)
      fires += tr.Feed(0.8f, false, ms, p).fired;
    check(fires == 0, "one speed spike plus slow movement is not an attack");
  }
  // Check the sample clock, not just the time of the previous attack.
  {
    SwingAttackTrigger tr;
    tr.Feed(0, false, 1000, p);
    tr.Feed(0, false, 1300, p);
    tr.Feed(3, false, 1400, p);
    check(!tr.Feed(3, false, 1350, p).fired && !tr.Armed(),
          "clock reversal before the first attack discards the armed swing");
  }
  {
    SwingAttackTrigger tr;
    tr.Feed(6, false, 0, p);
    tr.Feed(0, false, 250, p);
    check(!tr.Armed(), "moving initial sample cannot count as rest");
    auto bad = p;
    bad.hold_ms = 0;
    check(!tr.Feed(6, false, 500, bad).fired && !tr.Armed(),
          "invalid timing parameters fail closed");
  }

  if (failures) return 1;
  std::printf("swing_attack tests passed\n");
  return 0;
}
