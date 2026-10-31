#pragma once
// Eye-buffer sizing, kept out of xr.cpp so the rule is testable without a
// runtime. The runtime reports two sizes per view: a recommended image rect
// (what the compositor considers a fair cost) and a maximum (the largest it
// will accept). Both presentations pay the recommended cost, and nothing may
// switch to the maximum hint: on 30 Oct the theater view took the maximum,
// which forced a swapchain rebuild the moment the menu opened while
// g_screen_rect still held fractions of the previous eye extent. The headset
// then showed a black loading view, a wide blurry jagged menu, and a
// calibration panel stuck over the whole view after it was dismissed. One
// size for the whole session is what keeps that transition seamless.
//
// The backbuffer ceiling is load-bearing, not cosmetic. SteamVR reports a
// generic 8192x8192 maximum that is a format ceiling, not a render target: on
// 29 Oct the theater path took it literally and allocated 8192x8192x3 images
// per eye (~1.6 GB) to hold a 3440x1440 frame, and the headset showed black.
// Pixels beyond the source carry no extra detail, so the source size is the
// real maximum.
#include <algorithm>
#include <cstdint>

namespace dsvr {

struct EyeBufferSize {
  std::uint32_t width = 0, height = 0;
};

// scale_percent is the user's sharpness trim, clamped to the 50..200 band the
// mod menu offers. A zero maximum (older runtimes report none) leaves the
// scale unbounded rather than inventing a size. fallback_* is the game
// backbuffer, used only when the runtime reported no recommended rect.
inline EyeBufferSize ChooseEyeBufferSize(std::uint32_t rec_w, std::uint32_t rec_h,
                                         std::uint32_t max_w, std::uint32_t max_h,
                                         std::uint32_t fallback_w,
                                         std::uint32_t fallback_h,
                                         int scale_percent) {
  EyeBufferSize out;
  const std::uint32_t base_w = rec_w ? rec_w : fallback_w;
  const std::uint32_t base_h = rec_h ? rec_h : fallback_h;
  const int scale = std::clamp(scale_percent, 50, 200);
  const std::uint64_t limit_w = max_w ? max_w : UINT32_MAX;
  const std::uint64_t limit_h = max_h ? max_h : UINT32_MAX;
  out.width = static_cast<std::uint32_t>(
      std::max<std::uint64_t>(1, std::min<std::uint64_t>(
                                    static_cast<std::uint64_t>(base_w) * scale / 100, limit_w)));
  out.height = static_cast<std::uint32_t>(
      std::max<std::uint64_t>(1, std::min<std::uint64_t>(
                                     static_cast<std::uint64_t>(base_h) * scale / 100, limit_h)));
  return out;
}

}  // namespace dsvr
