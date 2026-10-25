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
inline constexpr int kSupportedFps[] = {60, 72, 80, 90, 120, 144, 240};
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
inline bool ShouldApplyNativeRate(int current_fps, int effective_fps) {
  return current_fps > 0 && IsSupportedFps(effective_fps) &&
         effective_fps != current_fps;
}

struct EffectiveRate {
  int requested_fps = 90;
  long long runtime_period_ns = 0;
  double runtime_hz = 0.0;          // application cadence, possibly reprojected
  double display_hz = 0.0;          // physical refresh, only when reported by XR
  bool from_display = false;
  bool runtime_throttled = false;
  int effective_fps = 90;
  bool from_runtime = false;
  bool mismatch = false;
  std::uint64_t generation = 0;
};

inline int SanitizeRequestedFps(int requested_fps) {
  if (IsSupportedFps(requested_fps)) return requested_fps;
  // Config load already restricts to the supported set; stay fail-safe.
  return 90;
}
inline EffectiveRate ResolveEffectiveRate(int requested_fps, long long period_ns,
                                           double display_hz = 0.0) {
  EffectiveRate out;
  out.requested_fps = SanitizeRequestedFps(requested_fps);
  out.effective_fps = out.requested_fps;
  const int q = QuantizePeriodToFps(period_ns);
  if (!q) return out;
  out.runtime_period_ns = period_ns;
  out.runtime_hz = 1e9 / (double)period_ns;
  out.from_runtime = true;
  out.from_display = std::isfinite(display_hz) && display_hz >= 59.5 &&
                     display_hz <= 240.5;
  if (out.from_display) {
    out.display_hz = display_hz;
    out.effective_fps = QuantizePeriodToFps(
        static_cast<long long>(std::llround(1e9 / display_hz)));
    out.runtime_throttled = std::isless(out.runtime_hz, display_hz - 0.5);
  } else {
    // Application periods below 60 Hz are not proof of a display refresh change.
    out.runtime_throttled = std::isless(out.runtime_hz, kSupportedFps[0] - 0.5);
    if (!out.runtime_throttled) out.effective_fps = q;
  }
  out.mismatch = out.effective_fps != out.requested_fps;
  return out;
}

// Hysteresis: the first live sample (and any loss to fallback) adopts
// immediately; a change between two live runtime rates must persist for
// kStableRateSamples consecutive samples so a noisy period cannot flap the
// game target. Single-writer (render thread); readers use snapshots.
class RateStabilizer {
 public:
  EffectiveRate Update(int requested_fps, long long period_ns,
                       double display_hz = 0.0) {
    EffectiveRate candidate = ResolveEffectiveRate(requested_fps, period_ns, display_hz);
    if (!candidate.from_runtime) {
      last_supported_fps_ = 0;
      Publish(candidate);
      ClearPending();
      return current_;
    }
    if (candidate.runtime_throttled && !candidate.from_display) {
      if (last_supported_fps_) candidate.effective_fps = last_supported_fps_;
      candidate.mismatch = candidate.effective_fps != candidate.requested_fps;
      Publish(candidate);
      ClearPending();
      return current_;
    }
    if (!last_supported_fps_ || candidate.effective_fps == current_.effective_fps) {
      Publish(candidate);
      last_supported_fps_ = candidate.effective_fps;
      ClearPending();
      return current_;
    }
    if (pending_fps_ == candidate.effective_fps) ++pending_count_;
    else { pending_fps_ = candidate.effective_fps; pending_count_ = 1; }
    if (pending_count_ >= kStableRateSamples) {
      Publish(candidate);
      last_supported_fps_ = candidate.effective_fps;
      ClearPending();
    }
    return current_;
  }
  const EffectiveRate& Current() const { return current_; }

 private:
  void Publish(const EffectiveRate& next) {
    const bool changed = current_.generation == 0 ||
        current_.effective_fps != next.effective_fps ||
        current_.requested_fps != next.requested_fps ||
        current_.from_runtime != next.from_runtime ||
        current_.from_display != next.from_display ||
        current_.runtime_throttled != next.runtime_throttled;
    const auto generation = current_.generation + (changed ? 1 : 0);
    current_ = next;
    current_.generation = generation;
  }
  void ClearPending() { pending_fps_ = 0; pending_count_ = 0; }
  EffectiveRate current_{};
  int pending_fps_ = 0;
  int pending_count_ = 0;
  int last_supported_fps_ = 0;
};

}  // namespace dsvr
