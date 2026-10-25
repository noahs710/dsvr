#include "../src/core/frame_pacing.h"

#include <cmath>
#include <cstdio>
#include <source_location>

int main() {
  int failures = 0;
  auto check = [&](bool ok, std::source_location at = std::source_location::current()) {
    if (!ok) { ++failures; std::printf("failed at line %u\n", at.line()); }
  };
  using namespace dsvr;
  const double k90 = 1000.0 / 90.0;
  const double k60 = 1000.0 / 60.0;

  // Empty window: no invented percentiles, no claimed match.
  {
    FramePacingTracker t;
    const auto w = t.Snapshot();
    check(w.frames == 0 && w.mean_ms == 0.0 && w.p50_ms == 0.0);
    check(w.Evenness() == 0.0 && !PacingMatchesTarget(w, 90));
  }

  // A perfectly even 90 Hz cadence reads as 90 Hz, evenly.
  {
    FramePacingTracker t;
    for (int i = 0; i < 900; ++i) t.Record(k90);
    const auto w = t.Snapshot();
    check(w.frames == 900);
    check(std::fabs(w.p50_ms - k90) < 0.25);
    check(w.p50_ms == w.p95_ms && w.p95_ms == w.p99_ms);
    check(w.Evenness() == 1.0);
    check(w.long_frames == 0 && w.short_frames == 0);
    check(std::fabs(w.MeanHz() - 90.0) < 0.05);
    check(PacingMatchesTarget(w, 90));
  }

  // The regression that matters: 2:1 judder averages to a clean 60 Hz. A
  // mean-only check passes it; the distribution must not.
  {
    FramePacingTracker t;
    for (int i = 0; i < 500; ++i) { t.Record(k90); t.Record(2.0 * k90); }
    const auto w = t.Snapshot();
    check(std::fabs(w.MeanHz() - 60.0) < 0.05);
    check(PacingMatchesTarget(w, 60));          // average looks legitimate
    check(w.p50_ms < 15.0 && w.p95_ms > 20.0);  // intervals do not
    check(w.long_frames == 500 && w.short_frames == 0);
    check(w.Evenness() < 0.2);
    check(!PacingMatchesTarget(w, 90));
  }

  // One held present inside an otherwise exact window is visible.
  {
    FramePacingTracker t;
    for (int i = 0; i < 999; ++i) t.Record(k90);
    t.Record(2.0 * k90);
    const auto w = t.Snapshot();
    check(w.long_frames == 1);
    check(w.p99_ms == w.p50_ms);  // one outlier does not move p99 at n=1000
  }

  // An early present is counted separately from a held one.
  {
    FramePacingTracker t;
    for (int i = 0; i < 100; ++i) t.Record(k60);
    for (int i = 0; i < 5; ++i) t.Record(5.0);
    const auto w = t.Snapshot();
    check(w.short_frames == 5 && w.long_frames == 0);
  }

  // Absurd samples are refused, not averaged into the distribution.
  {
    FramePacingTracker t;
    for (int i = 0; i < 10; ++i) t.Record(k90);
    t.Record(0.0);
    t.Record(-1.0);
    t.Record(NAN);
    t.Record(INFINITY);
    t.Record(101.0);  // a paused game / modal menu
    const auto w = t.Snapshot();
    check(w.frames == 10);
    check(w.rejected == 5);
    check(std::fabs(w.mean_ms - k90) < 0.25);
  }

  // min/max are real samples, not bucket edges, and the first sample seeds min.
  {
    FramePacingTracker t;
    t.Record(30.0);
    t.Record(k90);
    t.Record(20.0);
    const auto w = t.Snapshot();
    check(w.min_ms == k90 && w.max_ms == 30.0);
    check(w.min_ms < w.p50_ms && w.p50_ms <= w.max_ms);
  }

  // The log interval drains; the cumulative window keeps running.
  {
    FramePacingTracker t;
    for (int i = 0; i < 100; ++i) t.Record(k90);
    const auto first = t.TakeInterval();
    check(first.frames == 100 && first.p50_ms > 0.0);
    const auto drained = t.TakeInterval();
    check(drained.frames == 0 && drained.p50_ms == 0.0 && drained.Evenness() == 0.0);
    t.Record(k90);
    check(t.Snapshot().frames == 101);
    check(t.TakeInterval().frames == 1);
  }

  // Rejected samples are attributed to the interval that refused them too.
  {
    FramePacingTracker t;
    t.Record(-5.0);
    check(t.TakeInterval().rejected == 1);
    check(t.Snapshot().rejected == 1);
  }

  // Reset clears both windows, so a new session cannot inherit old evidence.
  {
    FramePacingTracker t;
    for (int i = 0; i < 50; ++i) t.Record(k90);
    t.Record(0.0);
    t.Reset();
    const auto w = t.Snapshot();
    check(w.frames == 0 && w.rejected == 0 && w.mean_ms == 0.0);
    check(t.TakeInterval().frames == 0);
  }

  // Out-of-range percentile queries refuse instead of clamping to a real bucket.
  {
    std::array<std::uint64_t, kPacingBucketCount> hist{};
    hist[(std::size_t)(k90 / kPacingBucketMs)] = 10;
    check(PacingPercentileMs(hist, 0.0) == 0.0);
    check(PacingPercentileMs(hist, 1.5) == 0.0);
    check(PacingPercentileMs(hist, 1.0) > 11.0 && PacingPercentileMs(hist, 1.0) < 11.5);
  }

  // Target matching is relative, so a 60 Hz title is judged against 60 Hz.
  {
    FramePacingTracker t;
    for (int i = 0; i < 300; ++i) t.Record(k60);
    const auto w = t.Snapshot();
    check(PacingMatchesTarget(w, 60));
    check(!PacingMatchesTarget(w, 72));
    check(!PacingMatchesTarget(w, 0));
    check(!PacingMatchesTarget(w, -10));
  }

  if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
  std::printf("frame_pacing: all checks passed\n");
  return 0;
}
