#include "../src/core/skill_flick_math.h"

#include <cmath>
#include <cstdio>

// Skill flourish bridge: a short weapon-hand impulse that returns to rest is
// one skill tap; anything sustained, too fast, or malformed is not.
int main() {
  int failures = 0;
  auto check = [&](bool ok, const char* what) {
    if (!ok) {
      std::printf("FAIL: %s\n", what);
      ++failures;
    }
  };
  using namespace dsvr;

  SkillFlickParams p;  // shipped defaults
  long long t = 0;
  auto frame = [&](SkillFlickTrigger& tr, float speed) {
    t += 11;  // 90 Hz
    return tr.Feed(speed, t, p);
  };

  // A flourish is fast for a moment and then back to rest.
  {
    SkillFlickTrigger tr;
    frame(tr, 0.1f);
    frame(tr, 0.1f);
    bool tapped = false;
    for (int i = 0; i < 4; ++i) tapped = frame(tr, 3.0f).tapped || tapped;
    check(!tapped, "a sustained fast hand is a swing, not a flourish");
  }
  {
    SkillFlickTrigger tr;
    frame(tr, 0.1f);
    frame(tr, 0.1f);
    for (int i = 0; i < 4; ++i) frame(tr, 3.0f);
    SkillFlickOutput last{};
    for (int i = 0; i < 30 && !last.tapped; ++i) last = frame(tr, 0.1f);
    check(last.tapped, "a flick that returns to rest taps once");
    check(last.tapping && tr.Tapping(), "the tap holds its press window");
    SkillFlickOutput held{};
    for (int i = 0; i < 20; ++i) held = frame(tr, 0.1f);
    check(!held.tapping && !tr.Tapping(), "the press window closes on its own");
  }
  // Swing-speed impulses and single-frame spikes are not flourishes.
  {
    SkillFlickTrigger tr;
    frame(tr, 0.1f);
    frame(tr, 0.1f);
    for (int i = 0; i < 4; ++i) frame(tr, 15.0f);
    SkillFlickOutput last{};
    for (int i = 0; i < 30; ++i) last = frame(tr, 0.1f);
    check(!last.tapped && !last.tapping,
          "an impulse above the band never taps");
  }
  {
    SkillFlickTrigger tr;
    frame(tr, 0.1f);
    frame(tr, 0.1f);
    frame(tr, 3.0f);
    SkillFlickOutput last{};
    for (int i = 0; i < 30; ++i) last = frame(tr, 0.1f);
    check(!last.tapped, "a single fast frame is a spike, not a flourish");
  }
  // Separate impulses without rest are not one flourish.
  {
    SkillFlickTrigger tr;
    frame(tr, 0.1f);
    frame(tr, 0.1f);
    for (int i = 0; i < 4; ++i) frame(tr, 3.0f);
    for (int i = 0; i < 4; ++i) frame(tr, 1.0f);  // slowing, never resting
    for (int i = 0; i < 4; ++i) frame(tr, 4.0f);
    SkillFlickOutput last{};
    for (int i = 0; i < 30; ++i) last = frame(tr, 0.1f);
    check(!last.tapped && !last.tapping,
          "impulses without rest are not one flourish");
  }
  // A completed flourish cannot immediately repeat: the cooldown holds.
  {
    SkillFlickTrigger tr;
    frame(tr, 0.1f);
    frame(tr, 0.1f);
    auto flourish = [&]() {
      for (int i = 0; i < 4; ++i) frame(tr, 3.0f);
      SkillFlickOutput last{};
      for (int i = 0; i < 30 && !last.tapped; ++i) last = frame(tr, 0.1f);
      return last.tapped;
    };
    check(flourish(), "first flourish taps");
    check(!flourish(), "the cooldown holds back-to-back flourishes");
    for (int i = 0; i < 70; ++i) frame(tr, 0.1f);  // 770 ms past the tap
    check(flourish(), "a flourish past the cooldown taps again");
  }
  // Fail-closed: bad input drops history and emits nothing.
  {
    SkillFlickTrigger tr;
    frame(tr, 0.1f);
    frame(tr, 0.1f);
    for (int i = 0; i < 4; ++i) frame(tr, 3.0f);
    check(!tr.Feed(3.0f, t - 1000, p).tapped && !tr.Feed(3.0f, t - 1000, p).tapping,
          "a backward clock drops the in-flight flourish");
    SkillFlickOutput bad = tr.Feed(std::nanf(""), t + 11, p);
    check(!bad.tapped && !bad.tapping, "a non-finite speed emits nothing");
  }

  if (failures) std::printf("skill_flick: %d FAILED\n", failures);
  else std::printf("skill_flick: all checks passed\n");
  return failures ? 1 : 0;
}
