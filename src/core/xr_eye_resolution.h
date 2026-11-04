#pragma once
#include <algorithm>
#include <cstdint>

namespace dsvr {

struct EyeBufferSize {
  std::uint32_t width = 0, height = 0;
};

// scale_percent is the user's sharpness trim, clamped to the 50..200 band the
// mod menu offers. A zero runtime maximum uses the D3D11 texture ceiling.
// fallback_* is the game
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
  const std::uint64_t limit_w = std::min<std::uint64_t>(max_w ? max_w : 16384, 16384);
  const std::uint64_t limit_h = std::min<std::uint64_t>(max_h ? max_h : 16384, 16384);
  if ((rec_w && rec_w > limit_w) || (rec_h && rec_h > limit_h)) return {};
  out.width = static_cast<std::uint32_t>(
      std::max<std::uint64_t>(1, std::min<std::uint64_t>(
                                    static_cast<std::uint64_t>(base_w) * scale / 100, limit_w)));
  out.height = static_cast<std::uint32_t>(
      std::max<std::uint64_t>(1, std::min<std::uint64_t>(
                                     static_cast<std::uint64_t>(base_h) * scale / 100, limit_h)));
  return out;
}

}  // namespace dsvr
