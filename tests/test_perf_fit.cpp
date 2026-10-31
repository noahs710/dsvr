// Hardware-tier presets only choose eye-buffer scale and antialiasing.
// Frame rates remain headset/monitor-owned and are not preset.
#include <cstdio>
#include <string>

#include "../src/core/perf_fit.h"

#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main() {
  using namespace dsvr;
  CHECK(TierForHardware({8, 8, 0}) == HardwareTier::kBalanced);
  CHECK(TierForHardware({6, 12, 1}) == HardwareTier::kPerformance);
  CHECK(TierForHardware({16, 4, 0}) == HardwareTier::kPerformance);
  CHECK(TierForHardware({16, 8, 0}) == HardwareTier::kBalanced);
  CHECK(TierForHardware({32, 12, 0}) == HardwareTier::kQuality);
  CHECK(TierForHardware({8, 0, 1}) == HardwareTier::kBalanced);
  CHECK(TierForHardware({8, 0, 0}) == HardwareTier::kPerformance);

  const Preset smooth = PresetForTier(HardwareTier::kPerformance, true);
  CHECK(std::string(smooth.id) == "auto-smooth" && smooth.render_scale_percent == 100);
  const Preset res = PresetForTier(HardwareTier::kPerformance, false);
  CHECK(std::string(res.id) == "auto-res" && res.render_scale_percent == 100);
  const Preset quality_smooth = PresetForTier(HardwareTier::kQuality, true);
  CHECK(quality_smooth.render_scale_percent == 100);
  CHECK(quality_smooth.aa_mode == AaMode::kMsaa4x);
  const Preset quality_res = PresetForTier(HardwareTier::kQuality, false);
  CHECK(quality_res.render_scale_percent == 130);
  CHECK(PresetForTier(HardwareTier::kBalanced, true).render_scale_percent == 100);

  CHECK(DefaultScaleForStereo(false) == 100);
  CHECK(DefaultScaleForStereo(true) == 85);
  std::puts("test_perf_fit: PASS");
  return 0;
}
