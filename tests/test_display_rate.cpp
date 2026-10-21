#include "../src/core/display_rate.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <source_location>

int main() {
  int failures = 0;
  auto check = [&](bool ok, std::source_location at = std::source_location::current()) {
    if (!ok) { ++failures; std::printf("failed at line %u\n", at.line()); }
  };
  using namespace dsvr;
  auto period = [](double hz) { return (long long)std::llround(1e9 / hz); };

  // Exact runtime rates quantize to themselves.
  check(QuantizePeriodToFps(period(60)) == 60);
  check(QuantizePeriodToFps(period(72)) == 72);
  check(QuantizePeriodToFps(11111111LL) == 90);  // canonical 90 Hz period
  check(QuantizePeriodToFps(period(120)) == 120);
  check(QuantizePeriodToFps(period(144)) == 144);
  check(QuantizePeriodToFps(period(240)) == 240);
  // Near misses snap to the nearest supported cap.
  check(QuantizePeriodToFps(period(89)) == 90);
  check(QuantizePeriodToFps(period(75)) == 72);
  // Midpoint between 72 and 90 is 81 Hz: at/above picks 90, below picks 72.
  check(QuantizePeriodToFps(period(80)) == 72);
  check(QuantizePeriodToFps(period(82)) == 90);
  // Out-of-span rates clamp to the nearest end; invalid periods refuse.
  check(QuantizePeriodToFps(period(30)) == 60);
  check(QuantizePeriodToFps(period(360)) == 240);
  check(QuantizePeriodToFps(0) == 0);
  check(QuantizePeriodToFps(-1) == 0);

  // Fallback: no live sample -> requested wins, no mismatch claimed.
  {
    EffectiveRate r = ResolveEffectiveRate(90, 0);
    check(!r.from_runtime && r.effective_fps == 90 && !r.mismatch);
    check(r.runtime_period_ns == 0 && r.runtime_hz == 0.0);
  }
  // Runtime drives: 72 Hz runtime overrides a 90 request and flags mismatch.
  {
    EffectiveRate r = ResolveEffectiveRate(90, period(72));
    check(r.from_runtime && r.effective_fps == 72 && r.mismatch);
    check(std::fabs(r.runtime_hz - 72.0) < 0.01);
  }
  // Matching runtime is not a mismatch; unsupported requests sanitize to 90.
  check(!ResolveEffectiveRate(90, period(90)).mismatch);
  check(ResolveEffectiveRate(61, 0).effective_fps == 90);
  check(ResolveEffectiveRate(90, period(59.94)).effective_fps == 60);
  check(!ResolveEffectiveRate(90, period(59.94)).runtime_throttled);
  check(ResolveEffectiveRate(90, period(45)).runtime_throttled);

  // Stabilizer: first live sample adopts at once; flapping does not.
  {
    RateStabilizer s;
    check(s.Update(90, 0).effective_fps == 90 && !s.Current().from_runtime);
    EffectiveRate first = s.Update(90, period(90));
    check(first.from_runtime && first.effective_fps == 90);
    const auto gen = first.generation;
    check(s.Update(90, period(72)).effective_fps == 90);   // 1 sample: hold
    check(s.Update(90, period(90)).effective_fps == 90);   // flap resets
    check(s.Update(90, period(72)).effective_fps == 90);   // 1
    check(s.Update(90, period(72)).effective_fps == 90);   // 2
    EffectiveRate adopted = s.Update(90, period(72));      // 3: adopt
    check(adopted.effective_fps == 72 && adopted.mismatch);
    check(adopted.generation > gen);
    // Loss falls back immediately, no hysteresis on the way down.
    EffectiveRate lost = s.Update(90, 0);
    check(!lost.from_runtime && lost.effective_fps == 90);
  }

  // Native-application decision: once per change, live owner only.
  check(ShouldApplyNativeRate(60, 72, 0));
  check(!ShouldApplyNativeRate(72, 72, 0));   // already at target
  check(!ShouldApplyNativeRate(60, 72, 72));  // already applied
  check(!ShouldApplyNativeRate(0, 72, 0));    // owner rate unknown
  check(!ShouldApplyNativeRate(60, 0, 0));    // no effective rate
  check(ShouldApplyNativeRate(60, 90, 72));   // new target after old apply

  // VDXR may throttle the next application frame to a divisor of the headset
  // cadence. Replaying the observed 90 -> 18 -> 90 sequence must not alternate
  // the native 90/60 cap and repeatedly clear the native pacing history.
  for (int display : {60, 72, 90, 120}) {
    RateStabilizer s;
    for (int i = 0; i < kStableRateSamples; ++i)
      s.Update(90, period(display));
    for (double hz : {45.0, 30.0, 22.5, 18.0, 90.0 / 7.0}) {
      EffectiveRate throttled;
      for (int i = 0; i < 10; ++i) throttled = s.Update(90, period(hz));
      check(throttled.from_runtime && throttled.effective_fps == display);
      check(std::fabs(throttled.runtime_hz - hz) < 0.01);
      check(throttled.runtime_period_ns == period(hz));
    }
  }
  // Starting under throttling uses the requested cap, and a real 60 Hz cadence
  // remains supported rather than being mistaken for an invalid frame period.
  {
    RateStabilizer s;
    check(s.Update(72, period(36)).effective_fps == 72);
    check(s.Current().from_runtime && std::fabs(s.Current().runtime_hz - 36) < 0.01);
    for (int i = 0; i < kStableRateSamples; ++i) s.Update(72, period(60));
    check(s.Current().effective_fps == 60);
    check(s.Current().mismatch);
  }
  {
    RateStabilizer s;
    check(s.Update(72, period(36)).effective_fps == 72);
    check(s.Update(90, period(45)).effective_fps == 90); // preference before live cadence
    s.Update(72, period(72));
    check(s.Update(90, period(36)).effective_fps == 72); // retain proven cadence
    s.Update(90, 0); // session loss clears the previous runtime cadence
    check(s.Update(90, period(45)).effective_fps == 90);
  }

  if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
  std::printf("display_rate: all checks passed\n");
  return 0;
}
