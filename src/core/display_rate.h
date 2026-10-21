#pragma once
#include <cmath>
#include <cstdint>

namespace dsvr {

// Runtime-driven display-rate matching. While an OpenXR session is live the
// runtime's predictedDisplayPeriod describes the next application frame, which
// may be throttled below the physical display cadence. Supported live cadences
// drive the cap; slower submission periods retain the last supported cadence
// (or configured preference before one is known). This header is
// pure (no XR/Windows/file deps) so the policy is unit-testable; xr.cpp owns
// publication and logging. Requested != actual stays visible: nothing here
// claims a native limiter has been applied.
inline constexpr int kSupportedFps[] = {60, 72, 90, 120, 144, 240};
inline constexpr int kStableRateSamples = 3;  // hysteresis, see RateStabilizer

inline bool IsSupportedFps(int fps) {
  for (int s : kSupportedFps) if (s == fps) return true;
  return false;
}
inline long long PeriodNsForFps(int fps) {
  return fps > 0 ? (long long)std::llround(1e9 / (double)fps) : 0;
}
// Nearest supported cap to the runtime rate; exact-half ties pick the lower
// rate (under-run the display rather than over-run it). 0 = invalid period.
inline int QuantizePeriodToFps(long long period_ns) {
  if (period_ns <= 0) return 0;
  const double hz = 1e9 / (double)period_ns;
  if (!std::isfinite(hz) || hz <= 0) return 0;
  int best = kSupportedFps[0];
  double best_dist = std::fabs(hz - best);
  for (int s : kSupportedFps) {
    const double d = std::fabs(hz - s);
    if (d < best_dist - 1e-9) { best = s; best_dist = d; }
  }
  return best;
}

// Native-application decision (frame_rate.cpp): apply once per change,
// only to a live owner with a known current rate.
inline bool ShouldApplyNativeRate(int current_fps, int effective_fps,
                                  int applied_fps) {
  return current_fps > 0 && effective_fps > 0 &&
         effective_fps != current_fps && effective_fps != applied_fps;
}

struct EffectiveRate {
  int requested_fps = 90;            // sanitized config fallback
  long long runtime_period_ns = 0;   // 0 = no live runtime sample
  double runtime_hz = 0.0;
  bool runtime_throttled = false;   // raw cadence is below native supported caps
  int effective_fps = 90;            // the single target consumers use
  bool from_runtime = false;
  bool mismatch = false;             // effective != requested
  std::uint64_t generation = 0;      // bumps on every adopted change
};

inline int SanitizeRequestedFps(int requested_fps) {
  if (IsSupportedFps(requested_fps)) return requested_fps;
  // Config load already restricts to the supported set; stay fail-safe.
  return 90;
}
inline EffectiveRate ResolveEffectiveRate(int requested_fps, long long period_ns) {
  EffectiveRate out;
  out.requested_fps = SanitizeRequestedFps(requested_fps);
  out.effective_fps = out.requested_fps;
  const int q = QuantizePeriodToFps(period_ns);
  if (!q) return out;
  out.runtime_period_ns = period_ns;
  out.runtime_hz = 1e9 / (double)period_ns;
  out.from_runtime = true;
  // Do not round 18/22.5/30/45 Hz reprojection periods up to 60 Hz. The native
  // setter clears its pacing epoch/history, so doing so feeds runtime
  // throttling back into native pacing. Allow normal 59.94 Hz clock rounding.
  out.runtime_throttled = out.runtime_hz < kSupportedFps[0] - 0.5;
  if (!out.runtime_throttled) out.effective_fps = q;
  out.mismatch = out.effective_fps != out.requested_fps;
  return out;
}

// Hysteresis: the first live sample (and any loss to fallback) adopts
// immediately; a change between two live runtime rates must persist for
// kStableRateSamples consecutive samples so a noisy period cannot flap the
// game target. Single-writer (render thread); readers use snapshots.
class RateStabilizer {
 public:
  EffectiveRate Update(int requested_fps, long long period_ns) {
    const EffectiveRate candidate = ResolveEffectiveRate(requested_fps, period_ns);
    if (!candidate.from_runtime) {
      last_supported_fps_ = 0;
      if (current_.from_runtime || current_.generation == 0) {
        Adopt(candidate);
      } else {
        current_.requested_fps = candidate.requested_fps;
        current_.effective_fps = candidate.effective_fps;
        current_.mismatch = false;
      }
      pending_fps_ = 0; pending_count_ = 0;
      return current_;
    }
    if (candidate.runtime_throttled) {
      EffectiveRate held = candidate;
      if (last_supported_fps_) held.effective_fps = last_supported_fps_;
      held.mismatch = held.effective_fps != held.requested_fps;
      if (!current_.from_runtime || !current_.runtime_throttled ||
          current_.effective_fps != held.effective_fps) Adopt(held);
      else {
        const auto generation = current_.generation;
        current_ = held;
        current_.generation = generation;
      }
      pending_fps_ = 0; pending_count_ = 0;
      return current_;
    }
    if (!last_supported_fps_) {
      Adopt(candidate); last_supported_fps_ = candidate.effective_fps;
      return current_;
    }
    if (candidate.effective_fps == current_.effective_fps) {
      if (current_.runtime_throttled) ++current_.generation;
      current_.runtime_period_ns = candidate.runtime_period_ns;
      current_.runtime_hz = candidate.runtime_hz;
      current_.runtime_throttled = false;
      current_.requested_fps = candidate.requested_fps;
      current_.mismatch = candidate.mismatch;
      pending_fps_ = 0; pending_count_ = 0;
      return current_;
    }
    if (pending_fps_ == candidate.effective_fps) ++pending_count_;
    else { pending_fps_ = candidate.effective_fps; pending_count_ = 1; }
    if (pending_count_ >= kStableRateSamples) {
      Adopt(candidate);
      last_supported_fps_ = candidate.effective_fps;
      pending_fps_ = 0; pending_count_ = 0;
    }
    return current_;
  }
  const EffectiveRate& Current() const { return current_; }

 private:
  void Adopt(const EffectiveRate& next) {
    const auto gen = current_.generation + 1;
    current_ = next;
    current_.generation = gen;
  }
  EffectiveRate current_{};
  int pending_fps_ = 0;
  int pending_count_ = 0;
  int last_supported_fps_ = 0;
};

}  // namespace dsvr
