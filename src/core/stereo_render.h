#pragma once
#include <cstdint>
#include <cstring>

#include "stereo_math.h"

namespace dsvr {

// Native stereo bracket state machine. The XR frame (xr_stereo_frame.h) owns
// prediction/image lifetimes; this bracket owns only the per-frame contract
// between XrBeginNativeStereoFrame and XrEndNativeStereoFrame: one token,
// two distinct borrowed targets, each eye drawn at most once, submit only
// with the full mask, and whole-pair cancel on any failure or timeout.
// It deliberately contains no native addresses: the draw callback is the
// seam where a proven main-scene span plugs in. Until that span is proven,
// no production hook installs this bracket and live output stays mono --
// wiring it to an unproven address would be exactly the blind replay the
// repo forbids. Dated static candidates are recorded in MODLOG.md; none is
// a qualified draw callback. A CPU wait is not GPU readiness.
struct StereoEyeInput {
  EyeFrustum frustum{};
  Vec3 origin{}, right{1, 0, 0}, up{0, 1, 0}, forward{0, 0, 1};
};

// Both eye clips or none: on failure every output is cleared.
inline bool BuildEyeClips(const float center[16], const StereoEyeInput eyes[2],
                          float out_clips[2][16]) {
  float built[2][16];
  for (unsigned eye = 0; eye < 2; ++eye) {
    if (!NativeEyeClip(center, eyes[eye].frustum, eyes[eye].origin,
                       eyes[eye].right, eyes[eye].up, eyes[eye].forward,
                       built[eye])) {
      std::memset(out_clips, 0, sizeof(built));
      return false;
    }
  }
  std::memcpy(out_clips, built, sizeof(built));
  return true;
}

class StereoBracket {
 public:
  using DrawFn = bool (*)(void* ctx, unsigned eye, void* texture2d);
  using SubmitFn = bool (*)(void* ctx, std::uint64_t token);

  struct Counters {
    std::uint64_t begun = 0, submitted = 0, cancelled = 0, eye_draws = 0;
  };

  // now_ms / timeout use the caller's monotonic clock (xr.cpp passes
  // GetTickCount64); the bracket itself reads no clock.
  bool Begin(std::uint64_t token, void* eye_textures[2],
             std::uint64_t now_ms, std::uint64_t timeout_ms = 100) {
    if (active_ || !token || !eye_textures[0] || !eye_textures[1] ||
        eye_textures[0] == eye_textures[1])
      return false;
    active_ = true;
    token_ = token;
    textures_[0] = eye_textures[0];
    textures_[1] = eye_textures[1];
    mask_ = 0;
    begin_ms_ = now_ms;
    timeout_ms_ = timeout_ms;
    ++counters_.begun;
    return true;
  }

  bool RenderEye(unsigned eye, DrawFn draw, void* ctx,
                 std::uint64_t now_ms) {
    if (!active_ || eye > 1 || !draw || (mask_ & (1u << eye)) ||
        TimedOut(now_ms)) {
      Cancel();
      return false;
    }
    if (!draw(ctx, eye, textures_[eye])) { Cancel(); return false; }
    mask_ |= (1u << eye);
    ++counters_.eye_draws;
    return true;
  }

  // Exactly mask 3 submits; anything else cancels the whole pair.
  bool Finish(std::uint64_t token, SubmitFn submit, void* ctx,
              std::uint64_t now_ms) {
    if (!active_ || !submit || token != token_ || mask_ != 3u ||
        TimedOut(now_ms)) {
      Cancel();
      return false;
    }
    const bool ok = submit(ctx, token_);
    active_ = false;
    token_ = 0;
    mask_ = 0;
    if (ok) ++counters_.submitted; else ++counters_.cancelled;
    return ok;
  }

  void Cancel() {
    if (!active_) return;
    active_ = false;
    token_ = 0;
    mask_ = 0;
    ++counters_.cancelled;
  }

  bool Active() const { return active_; }
  bool TimedOut(std::uint64_t now_ms) const {
    return active_ && now_ms >= begin_ms_ &&
           (now_ms - begin_ms_) > timeout_ms_;
  }
  unsigned RenderedMask() const { return mask_; }
  std::uint64_t Token() const { return token_; }
  const Counters& Stats() const { return counters_; }

 private:
  void* textures_[2] = {nullptr, nullptr};
  std::uint64_t token_ = 0, begin_ms_ = 0, timeout_ms_ = 100;
  unsigned mask_ = 0;
  bool active_ = false;
  Counters counters_{};
};

}  // namespace dsvr
