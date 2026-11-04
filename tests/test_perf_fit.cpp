// Hardware-tier fit only chooses eye-buffer scale and antialiasing.
// Frame rates remain headset/monitor-owned and are not preset.
#include <cstdio>

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

  // The fit must stay inside the supported completed-image AA modes (0/1) and
  // the render-scale range ConfigValid accepts; the old MSAA selection made a
  // quality-tier preset fail validation.
  const HardwareTier tiers[] = {HardwareTier::kPerformance,
                                HardwareTier::kBalanced,
                                HardwareTier::kQuality};
  for (HardwareTier tier : tiers) {
    const AutoFit fit = AutoFitForTier(tier);
    CHECK(fit.aa_mode == 0 || fit.aa_mode == 1);
    CHECK(fit.render_scale_percent >= 50 && fit.render_scale_percent <= 200);
  }
  CHECK(AutoFitForTier(HardwareTier::kPerformance).render_scale_percent == 80);
  CHECK(AutoFitForTier(HardwareTier::kBalanced).render_scale_percent == 100);
  CHECK(AutoFitForTier(HardwareTier::kQuality).render_scale_percent == 130);

  std::puts("test_perf_fit: PASS");
  return 0;
}
