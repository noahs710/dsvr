// Arm-swing locomotion bridge tests: a running gait is an alternation pattern,
// so the tests are about which two-hand movements are NOT a run.
#include "../src/core/arm_swing_math.h"

#include <cmath>
#include <cstdio>

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
  if (!ok) { ++g_fail; std::printf("FAIL: %s\n", what); }
}

using dsvr::ArmSwingOutput;
using dsvr::ArmSwingParams;
using dsvr::ArmSwingSprint;

// Feed `frames` frames 10 ms apart at constant hand speeds; return the last.
ArmSwingOutput hold(ArmSwingSprint& tr, long long& t, float left, float right,
                    const ArmSwingParams& p, int frames) {
  ArmSwingOutput out{};
  for (int i = 0; i < frames; ++i) { out = tr.Feed(left, right, t, p); t += 10; }
  return out;
}

// One running stride: `speed` for 200 ms, then 200 ms of quiet.
ArmSwingOutput stride(ArmSwingSprint& tr, long long& t, bool left, float speed,
                      const ArmSwingParams& p) {
  hold(tr, t, left ? speed : 0.2f, left ? 0.2f : speed, p, 20);
  return hold(tr, t, 0.2f, 0.2f, p, 20);
}

// Run `n` alternating strides starting with `first_left`.
bool engage(ArmSwingSprint& tr, long long& t, const ArmSwingParams& p, int n,
            bool first_left = true) {
  bool sprinted = false;
  for (int i = 0; i < n; ++i) {
    if (stride(tr, t, (i % 2 == 0) == first_left, 3.0f, p).sprint) sprinted = true;
  }
  return sprinted;
}

void AlternatingStridesEngage() {
  ArmSwingParams p;
  ArmSwingSprint tr;
  long long t = 1000;
  float peak_strength = 0.0f;
  bool sprinted = false;
  for (int i = 0; i < 6; ++i) {
    const auto out = stride(tr, t, i % 2 == 0, 3.0f, p);
    if (out.sprint) sprinted = true;
    if (out.strength > peak_strength) peak_strength = out.strength;
  }
  check(sprinted, "loose alternation engages the run");
  check(peak_strength > 0.5f && peak_strength <= 1.0f,
        "the forward amount ramps with the stride rate");
  check(tr.Alternations() == 5, "one swap per stride pair");

  // Full ramp: keep the gait going and the amount reaches the ceiling, never past it.
  ArmSwingSprint tr2;
  long long t2 = 1000;
  float last = 0.0f;
  for (int i = 0; i < 40; ++i) {
    const auto out = stride(tr2, t2, i % 2 == 0, 3.0f, p);
    if (out.sprint) last = out.strength;
  }
  check(last == 1.0f, "the forward amount saturates at full run");
}

void OneHandFlapping() {
  ArmSwingParams p;
  ArmSwingSprint tr;
  long long t = 1000;
  bool any = false;
  for (int i = 0; i < 12; ++i) {
    if (stride(tr, t, true, 3.0f, p).sprint) any = true;
  }
  check(!any, "one hand flapping alone is never a run");
  check(tr.Alternations() == 0, "a same-hand repeat restarts the swap count");
}

void BothHandsTogether() {
  ArmSwingParams p;
  ArmSwingSprint tr;
  long long t = 1000;
  bool any = false;
  for (int i = 0; i < 8; ++i) {
    if (hold(tr, t, 3.0f, 3.0f, p, 20).sprint) any = true;
    if (hold(tr, t, 0.2f, 0.2f, p, 20).sprint) any = true;
  }
  check(!any, "both hands moving together is a swing, not a gait");

  // Staggered onsets: the second hand starting cancels the phase already open,
  // so a two-handed stretch that does not start in the same frame still fails.
  ArmSwingSprint tr2;
  long long t2 = 1000;
  bool any2 = false;
  for (int i = 0; i < 8; ++i) {
    if (hold(tr2, t2, 3.0f, 0.2f, p, 5).sprint) any2 = true;
    if (hold(tr2, t2, 3.0f, 3.0f, p, 15).sprint) any2 = true;
    if (hold(tr2, t2, 0.2f, 0.2f, p, 20).sprint) any2 = true;
  }
  check(!any2, "an overlapping second hand cancels the stride it overlapped");
}

void OutOfBandMotion() {
  ArmSwingParams p;
  // Too slow: a gentle arm swing is walking, and walking is already handled by
  // the roomscale sampler.
  {
    ArmSwingSprint tr;
    long long t = 1000;
    bool any = false;
    for (int i = 0; i < 10; ++i) {
      if (stride(tr, t, i % 2 == 0, 1.0f, p).sprint) any = true;
    }
    check(!any, "a gentle arm swing stays below the stride band");
  }
  // Too fast: that is a weapon swing, and the attack bridge owns it.
  {
    ArmSwingSprint tr;
    long long t = 1000;
    bool any = false;
    for (int i = 0; i < 10; ++i) {
      if (stride(tr, t, i % 2 == 0, 12.0f, p).sprint) any = true;
    }
    check(!any, "a swing above the band never becomes a run");
  }
  // A fast phase that never ends is a reach, not a stride.
  {
    ArmSwingSprint tr;
    long long t = 1000;
    bool any = false;
    for (int i = 0; i < 6; ++i) {
      if (hold(tr, t, i % 2 == 0 ? 3.0f : 0.2f, i % 2 == 0 ? 0.2f : 3.0f, p, 120)
              .sprint)
        any = true;
    }
    check(!any, "a fast phase held past the stride window is a reach, not a stride");
  }
}

void SprintLingersThenReleases() {
  ArmSwingParams p;
  ArmSwingSprint tr;
  long long t = 1000;
  check(engage(tr, t, p, 6), "the gait engages");
  check(tr.Sprinting(), "the gait is engaged after the last stride");
  hold(tr, t, 0.2f, 0.2f, p, 30);  // 300 ms: shorter than hold_ms
  check(tr.Sprinting(), "the run survives a short pause between strides");
  hold(tr, t, 0.2f, 0.2f, p, 50);  // 1000 ms since the last stride: past hold_ms
  check(!tr.Sprinting(), "the run releases once the strides stop");
  check(tr.Alternations() == 0, "a released run clears its swap count");
}

void AGapLongerThanOneGait() {
  ArmSwingParams p;
  ArmSwingSprint tr;
  long long t = 1000;
  engage(tr, t, p, 4);
  // A pause past hold_ms drops the run, and one stride after the pause is not
  // enough to bring it back: the pattern has to be running again.
  hold(tr, t, 0.2f, 0.2f, p, 50);
  const auto resumed = stride(tr, t, false, 3.0f, p);
  check(!resumed.sprint, "a stride after a long pause does not resume the run alone");
  check(engage(tr, t, p, 4), "the run re-engages once the pattern is running again");
}

void ClockAndNumbers() {
  ArmSwingParams p;
  ArmSwingSprint tr;
  long long t = 1000;
  engage(tr, t, p, 6);
  const auto back = tr.Feed(3.0f, 0.2f, t - 50, p);
  check(!back.sprint && !tr.Sprinting(),
        "a clock that steps behind clears the run rather than replaying it");

  ArmSwingSprint tr2;
  long long t2 = 1000;
  engage(tr2, t2, p, 6);
  const auto nan = tr2.Feed(std::nanf(""), 0.2f, t2, p);
  check(!nan.sprint && !tr2.Sprinting(), "a non-finite hand speed clears the run");
  const auto neg = tr2.Feed(-1.0f, 0.2f, t2, p);
  check(!neg.sprint, "a negative hand speed is refused");
  check(engage(tr2, t2, p, 6), "the run recovers after a refused sample");
}

void ImpossibleParamsRefuse() {
  long long t = 1000;
  {
    ArmSwingParams p;
    p.stride_speed_max = 1.0f;  // below the stride floor
    ArmSwingSprint tr;
    check(!engage(tr, t, p, 8), "an inverted speed band refuses the run");
  }
  {
    ArmSwingParams p;
    p.quiet_speed_max = 2.0f;  // the quiet ceiling sits above the stride floor
    ArmSwingSprint tr;
    check(!engage(tr, t, p, 8), "a quiet band that swallows the stride refuses the run");
  }
  {
    ArmSwingParams p;
    p.alternations_min = 1;  // one swap is not a gait
    ArmSwingSprint tr;
    check(!engage(tr, t, p, 8), "a single swap is not enough to call it a run");
  }
  {
    ArmSwingParams p;
    p.ramp_alternations = 2;  // ramp below the engage threshold
    ArmSwingSprint tr;
    check(!engage(tr, t, p, 8), "a ramp below the engage threshold refuses the run");
  }
}

void IdenticalInputsIdenticalOutput() {
  // The AGENTS.md lesson: duplicate callbacks and millisecond boundaries must
  // reproduce the same gait, so run the same script twice and compare frames.
  ArmSwingParams p;
  ArmSwingSprint a, b;
  long long ta = 1000, tb = 1000;
  bool same = true;
  for (int i = 0; i < 200; ++i) {
    const bool left = (i / 20) % 2 == 0;
    const float fast = 2.4f + 0.01f * static_cast<float>(i % 7);
    const float l = (i % 40) < 20 ? (left ? fast : 0.1f) : 0.1f;
    const float r = (i % 40) < 20 ? (left ? 0.1f : fast) : 0.1f;
    const auto oa = a.Feed(l, r, ta, p);
    ta += 10;
    const auto ob = b.Feed(l, r, tb, p);
    tb += 10;
    if (oa.sprint != ob.sprint || oa.strength != ob.strength ||
        oa.alternations != ob.alternations)
      same = false;
  }
  check(same, "the same hand history always produces the same gait");
}

}  // namespace

int main() {
  AlternatingStridesEngage();
  OneHandFlapping();
  BothHandsTogether();
  OutOfBandMotion();
  SprintLingersThenReleases();
  AGapLongerThanOneGait();
  ClockAndNumbers();
  ImpossibleParamsRefuse();
  IdenticalInputsIdenticalOutput();
  if (g_fail) {
    std::printf("%d arm_swing test(s) failed\n", g_fail);
    return 1;
  }
  std::printf("arm_swing tests passed\n");
  return 0;
}
