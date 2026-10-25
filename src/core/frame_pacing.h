#pragma once

#include <array>
#include <cmath>
#include <cstdint>

#include "display_rate.h"

namespace dsvr {

// Frame pacing is the consistency of the interval between successive displayed
// frames, not the rate value. A title can average exactly the runtime's
// requested 90 Hz and still feel wrong if the intervals alternate 11.1/22.2 ms
// (2:1 judder): the mean hides it, the distribution exposes it. K1 asks for
// p50/p95/p99 plus frame drops, so the mean/max pair in cpu_frame_timer.h is
// not enough evidence on its own.
//
// Fixed storage, no allocation, no IO, no GPU query. One writer (the render
// thread at the native frame boundary); readers take value snapshots.
inline constexpr double kPacingBucketMs = 0.25;
inline constexpr std::size_t kPacingBucketCount = 400;  // 0 .. 100 ms
inline constexpr double kPacingMaxSampleMs = 100.0;
// A present this far from the window median is a repeated or dropped frame
// rather than ordinary submission jitter.
inline constexpr double kPacingLongRatio = 1.5;

// Nearest-rank percentile over the fixed linear histogram, reported at the
// bucket midpoint so a single exact cadence reads as that cadence and not as
// its lower bucket edge. 0 when the window is empty.
inline double PacingPercentileMs(
    const std::array<std::uint64_t, kPacingBucketCount>& hist, double q) {
  std::uint64_t total = 0;
  for (std::uint64_t c : hist) total += c;
  if (!total || !(q > 0.0) || q > 1.0) return 0.0;
  std::uint64_t rank = (std::uint64_t)std::llround(q * (double)total);
  if (rank < 1) rank = 1;
  std::uint64_t cumulative = 0;
  for (std::size_t i = 0; i < hist.size(); ++i) {
    cumulative += hist[i];
    if (cumulative >= rank) return ((double)i + 0.5) * kPacingBucketMs;
  }
  return (double)(hist.size() - 0.5) * kPacingBucketMs;
}

struct PacingWindow {
  std::uint64_t frames = 0;
  double mean_ms = 0.0, min_ms = 0.0, max_ms = 0.0;
  double p50_ms = 0.0, p95_ms = 0.0, p99_ms = 0.0;
  // Buckets beyond kPacingLongRatio x the median: a present held for two
  // display intervals, or a submission that arrived early.
  std::uint64_t long_frames = 0, short_frames = 0;
  // Samples refused by Record: non-positive, non-finite, or outside the span.
  std::uint64_t rejected = 0;
  double MeanHz() const { return mean_ms > 0.0 ? 1000.0 / mean_ms : 0.0; }
  // 1.0 = every interval identical; 0.0 = the p95..p50 spread is as wide as
  // the median itself. Deliberately spread-relative, so a 90 Hz title and a
  // 60 Hz title are judged on the same scale.
  double Evenness() const {
    if (!(p50_ms > 0.0)) return 0.0;
    const double spread = p95_ms - p50_ms;
    if (spread <= 0.0) return 1.0;
    return spread < p50_ms ? 1.0 - spread / p50_ms : 0.0;
  }
};

// True only when the measured mean interval really is the runtime's cadence.
// A matching average with bad evenness is still not a pass -- the caller has
// to read both numbers.
inline bool PacingMatchesTarget(const PacingWindow& window, int target_fps,
                                double tolerance = 0.05) {
  const double target_ms = (double)PeriodNsForFps(target_fps) / 1e6;
  if (!(target_ms > 0.0) || !(window.mean_ms > 0.0)) return false;
  return std::fabs(window.mean_ms - target_ms) <= target_ms * tolerance;
}

class FramePacingTracker {
 public:
  // Wall time between the start of consecutive native frame boundaries.
  // Absurd samples (a paused game, a modal menu, a session gap) are refused
  // and counted rather than averaged into the distribution.
  void Record(double interval_ms) {
    if (!(interval_ms > 0.0) || interval_ms > kPacingMaxSampleMs ||
        !std::isfinite(interval_ms)) {
      ++stats_.rejected;
      ++interval_.rejected;
      return;
    }
    std::size_t bucket = (std::size_t)(interval_ms / kPacingBucketMs);
    if (bucket >= kPacingBucketCount) bucket = kPacingBucketCount - 1;
    if (!stats_.frames || interval_ms < stats_.min_ms) stats_.min_ms = interval_ms;
    if (interval_ms > stats_.max_ms) stats_.max_ms = interval_ms;
    if (!interval_.frames || interval_ms < interval_.min_ms)
      interval_.min_ms = interval_ms;
    if (interval_ms > interval_.max_ms) interval_.max_ms = interval_ms;
    ++stats_.hist[bucket];
    ++interval_.hist[bucket];
    ++stats_.frames; ++interval_.frames;
    stats_.total_ms += interval_ms; interval_.total_ms += interval_ms;
  }
  PacingWindow Snapshot() const { return Summarize(stats_); }
  // Drain the log interval only; the cumulative window keeps running.
  PacingWindow TakeInterval() {
    const PacingWindow result = Summarize(interval_);
    interval_ = {};
    return result;
  }
  void Reset() { stats_ = {}; interval_ = {}; }

 private:
  struct Accumulator {
    std::array<std::uint64_t, kPacingBucketCount> hist{};
    std::uint64_t frames = 0, rejected = 0;
    double total_ms = 0.0, min_ms = 0.0, max_ms = 0.0;
  };
  static PacingWindow Summarize(const Accumulator& acc) {
    PacingWindow w;
    w.frames = acc.frames;
    w.rejected = acc.rejected;
    if (!acc.frames) return w;
    w.mean_ms = acc.total_ms / (double)acc.frames;
    w.min_ms = acc.min_ms;
    w.max_ms = acc.max_ms;
    w.p50_ms = PacingPercentileMs(acc.hist, 0.50);
    w.p95_ms = PacingPercentileMs(acc.hist, 0.95);
    w.p99_ms = PacingPercentileMs(acc.hist, 0.99);
    const double hi = w.p50_ms * kPacingLongRatio;
    const double lo = w.p50_ms / kPacingLongRatio;
    for (std::size_t i = 0; i < acc.hist.size(); ++i) {
      if (!acc.hist[i]) continue;
      const double midpoint = ((double)i + 0.5) * kPacingBucketMs;
      if (midpoint > hi) w.long_frames += acc.hist[i];
      else if (midpoint < lo) w.short_frames += acc.hist[i];
    }
    return w;
  }
  Accumulator stats_{}, interval_{};
};

}  // namespace dsvr
