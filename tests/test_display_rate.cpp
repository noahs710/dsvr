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

  check(QuantizePeriodToFps(period(60)) == 60);
  check(QuantizePeriodToFps(period(72)) == 72);
  check(QuantizePeriodToFps(period(75)) == 75);
  check(QuantizePeriodToFps(period(80)) == 80);
  check(QuantizePeriodToFps(period(82)) == 82);
  check(QuantizePeriodToFps(period(89)) == 89);
  check(QuantizePeriodToFps(period(120)) == 120);
  check(QuantizePeriodToFps(period(240)) == 240);
  check(QuantizePeriodToFps(period(360)) == 360);
  check(QuantizePeriodToFps(0) == 0);
  check(QuantizePeriodToFps(-1) == 0);

  const EffectiveRate no_sample = ResolveEffectiveRate(0, 0.0);
  check(!no_sample.from_runtime && no_sample.effective_fps == 0);
  const EffectiveRate live = ResolveEffectiveRate(period(72));
  check(live.from_runtime && live.effective_fps == 72 && live.runtime_hz > 71.9);
  const EffectiveRate throttled = ResolveEffectiveRate(period(45), 90.0);
  check(throttled.from_runtime && throttled.from_display &&
        throttled.effective_fps == 90 && throttled.runtime_throttled);

  {
    RateStabilizer s;
    check(s.Update(0).effective_fps == 0 && !s.Current().from_runtime);
    const EffectiveRate first = s.Update(period(90), 90.0);
    check(first.from_runtime && first.effective_fps == 90);
    const auto generation = first.generation;
    check(s.Update(period(72), 72.0).effective_fps == 90);
    check(s.Update(period(72), 72.0).effective_fps == 90);
    const EffectiveRate changed = s.Update(period(72), 72.0);
    check(changed.effective_fps == 72 && changed.generation > generation);
    const EffectiveRate lost = s.Update(0);
    check(!lost.from_runtime && lost.effective_fps == 0);
  }

  for (double display : {53.0, 59.94, 72.0, 80.0, 90.0, 119.88, 144.0, 240.0}) {
    const int rounded = (int)(display + 0.5);
    RateStabilizer s;
    EffectiveRate live_display;
    for (int i = 0; i < kStableRateSamples; ++i)
      live_display = s.Update(period(display), display);
    check(live_display.from_display && live_display.effective_fps == rounded);
    for (int divisor : {2, 3, 4}) {
      const EffectiveRate held = s.Update(period(display / divisor), display);
      check(held.effective_fps == rounded);
      check(held.runtime_throttled && held.from_display);
    }
  }

  if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
  std::printf("display_rate: all checks passed\n");
  return 0;
}
