#pragma once
#include <cmath>
#include <cstdint>

namespace dsvr {

// Runtime/display-rate observation only. It never chooses or applies a game cap.
// XR owns publication/logging; the dependency owns FPSUnlock, MaxFPS and frame time.

inline constexpr int kStableRateSamples = 3;  // hysteresis, see RateStabilizer


inline long long PeriodNsForFps(int fps) {
  return fps > 0 ? (long long)std::llround(1e9 / (double)fps) : 0;
}
// Round an observed cadence to the nearest whole Hz. 0 means no valid sample.
inline int QuantizePeriodToFps(long long period_ns) {
  if (period_ns <= 0) return 0;
  const double hz = 1e9 / static_cast<double>(period_ns);
  if (!std::isfinite(hz) || hz < 10.0 || hz > 1000.0) return 0;
  return static_cast<int>(std::lround(hz));
}

struct EffectiveRate {
  long long runtime_period_ns = 0;
  double runtime_hz = 0.0;
  double display_hz = 0.0;
  int effective_fps = 0;
  bool from_runtime = false;
  bool from_display = false;
  bool runtime_throttled = false;
  std::uint64_t generation = 0;
};


inline EffectiveRate ResolveEffectiveRate(long long period_ns,
                                         double display_hz = 0.0) {
  EffectiveRate out{};
  const int observed = QuantizePeriodToFps(period_ns);
  if (observed == 0) return out;
  out.runtime_period_ns = period_ns;
  out.runtime_hz = 1e9 / (double)period_ns;
  out.from_runtime = true;
  const bool valid_display = std::isfinite(display_hz) &&
      std::isgreaterequal(display_hz, 10.0) &&
      std::islessequal(display_hz, 1000.0);
  if (valid_display) {
    out.from_display = true;
    out.display_hz = display_hz;
    out.effective_fps = (int)(display_hz + 0.5);
    out.runtime_throttled = std::isless(out.runtime_hz, display_hz - 0.5);
  } else {
    out.effective_fps = observed;
  }
  return out;
}

// Hysteresis stabilizes the observed runtime cadence for three samples. This
// snapshot is reporting-only and never sets a game target.
class RateStabilizer {
 public:
  EffectiveRate Update(long long period_ns, double display_hz = 0.0) {
    EffectiveRate candidate = ResolveEffectiveRate(period_ns, display_hz);
    if (!candidate.from_runtime) {
      last_observed_fps_ = 0;
      Publish(candidate);
      ClearPending();
      return current_;
    }
    if (candidate.runtime_throttled && !candidate.from_display) {
      if (last_observed_fps_) candidate.effective_fps = last_observed_fps_;
      Publish(candidate);
      ClearPending();
      return current_;
    }
    if (!last_observed_fps_ || candidate.effective_fps == current_.effective_fps) {
      Publish(candidate);
      last_observed_fps_ = candidate.effective_fps;
      ClearPending();
      return current_;
    }
    if (pending_fps_ == candidate.effective_fps) ++pending_count_;
    else { pending_fps_ = candidate.effective_fps; pending_count_ = 1; }
    if (pending_count_ >= kStableRateSamples) {
      Publish(candidate);
      last_observed_fps_ = candidate.effective_fps;
      ClearPending();
    }
    return current_;
  }
  const EffectiveRate& Current() const { return current_; }

 private:
  void Publish(const EffectiveRate& next) {
    const bool changed = current_.generation == 0 ||
        current_.effective_fps != next.effective_fps ||
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
  int last_observed_fps_ = 0;
};

}  // namespace dsvr
