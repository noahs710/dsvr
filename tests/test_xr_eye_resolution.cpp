// Eye-buffer sizing: both presentations use the runtime's recommended rect,
// no presentation change may rebuild the swapchains, and the maximum hint is
// only ever a ceiling - never a base, and never above the backbuffer whose
// frame is copied.
#include "../src/core/xr_eye_resolution.h"

#include <cstdio>

namespace {

int g_failures = 0;

void Check(bool ok, const char* what) {
  if (!ok) {
    ++g_failures;
    std::printf("FAIL %s\n", what);
  }
}

}  // namespace

int main() {
  using dsvr::ChooseEyeBufferSize;
  using dsvr::EyeBufferSize;
  const auto odd = ChooseEyeBufferSize(2145, 2293, 8192, 8192, 640, 480, 100);
  Check(odd.width == 2145 && odd.height == 2293, "100 percent preserves odd recommendations exactly");
  const auto invalid = ChooseEyeBufferSize(2145, 2293, 2000, 8192, 640, 480, 100);
  Check(invalid.width == 0 && invalid.height == 0, "invalid recommendation fails instead of silently shrinking 100 percent");

  // Immersive play: recommended rect, no max hint.
  const EyeBufferSize immersive = ChooseEyeBufferSize(2064, 2208, 2464, 2448, 1920, 1080, 100);
  Check(immersive.width == 2064 && immersive.height == 2208, "immersive uses recommended rect");

  // Theater must not switch hints: the same recommended base as immersive, so
  // opening the menu never rebuilds a live swapchain.
  const EyeBufferSize theater = ChooseEyeBufferSize(2064, 2208, 2464, 2448, 1920, 1080, 100);
  Check(theater.width == immersive.width && theater.height == immersive.height,
        "one size for every presentation, so no presentation change rebuilds");

  // The regression that blacked out the headset: SteamVR reports a generic
  // 8192x8192 format ceiling, and taking it literally asked for 8192x8192x3
  // images per eye to hold a 3440x1440 frame.
  const EyeBufferSize steamvr = ChooseEyeBufferSize(2144, 2292, 8192, 8192, 3440, 1440, 100);
  Check(steamvr.width == 2144 && steamvr.height == 2292, "a generic runtime ceiling is never the base");

  // A scale trim above 100% raises the base but never past the runtime maximum.
  const EyeBufferSize boosted = ChooseEyeBufferSize(2064, 2208, 2464, 2448, 3840, 2160, 150);
  Check(boosted.width == 2464 && boosted.height == 2448, "a scale trim above 100% clamps to the maximum");

  // Older runtimes report no maximum: fall back, never invent a size.
  const EyeBufferSize no_max = ChooseEyeBufferSize(2064, 2208, 0, 0, 1920, 1080, 100);
  Check(no_max.width == 2064 && no_max.height == 2208, "missing maximum still uses the recommended rect");

  // Nothing at all reported: fall back to the backbuffer, and never zero.
  const EyeBufferSize bare = ChooseEyeBufferSize(0, 0, 0, 0, 1920, 1080, 50);
  Check(bare.width == 960 && bare.height == 540, "scale trim applies to the backbuffer fallback");
  const EyeBufferSize degenerate = ChooseEyeBufferSize(0, 0, 0, 0, 0, 0, 100);
  Check(degenerate.width >= 1 && degenerate.height >= 1, "never requests a zero-sized swapchain");

  if (g_failures) {
    std::printf("xr_eye_resolution: %d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("xr_eye_resolution: all checks passed\n");
  return 0;
}
