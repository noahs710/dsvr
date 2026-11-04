// The shared user-preset table: unique stable ids, every row usable, and each
// style preset owning only its documented fields.
#include <cstdio>
#include <cstring>
#include <source_location>

#include "../src/core/user_presets.h"

int main() {
  int failures = 0;
  auto check = [&](bool ok, std::source_location at = std::source_location::current()) {
    if (!ok) {
      ++failures;
      std::printf("failed at line %u\n", at.line());
    }
  };
  using namespace dsvr;

  // Table integrity: stable unique verbs and labels, and every row findable.
  check(kUserPresetCount == 4);
  for (std::size_t i = 0; i < kUserPresetCount; ++i) {
    const UserPresetInfo& a = kUserPresets[i];
    check(a.verb && *a.verb && a.label && *a.label && a.detail && *a.detail);
    check(FindUserPreset(a.verb) == &a);
    check(a.hardware_fit == (a.id == UserPreset::kMatchPc));
    for (std::size_t j = i + 1; j < kUserPresetCount; ++j) {
      check(std::strcmp(a.verb, kUserPresets[j].verb) != 0);
      check(std::strcmp(a.label, kUserPresets[j].label) != 0);
    }
  }
  check(FindUserPreset(nullptr) == nullptr);
  check(FindUserPreset("") == nullptr);
  check(FindUserPreset("legacy-name") == nullptr);

  // Comfort: gentle scale, strong vignette, snap turning.
  {
    Config cfg = FactorySafeDefaults();
    check(ApplyUserPreset(cfg, UserPreset::kComfort));
    check(cfg.comfort_vignette && cfg.vignette_strength >= 0.8f);
    check(cfg.fov_scale < 1.0f);
    check(cfg.snap_turn && !cfg.smooth_turn && cfg.snap_turn_degrees == 30);
    check(cfg.render_scale_percent == 90 && cfg.aa_mode == 1);
  }

  // Balanced: the recommended start, mild vignette, smooth turning, full scale.
  {
    Config cfg = FactorySafeDefaults();
    check(ApplyUserPreset(cfg, UserPreset::kBalanced));
    check(cfg.comfort_vignette && cfg.vignette_strength < 0.8f);
    check(!cfg.snap_turn && cfg.smooth_turn);
    check(cfg.render_scale_percent == 100 && cfg.aa_mode == 1);
  }

  // Sharper: no vignette, free turning, full scale.
  {
    Config cfg = FactorySafeDefaults();
    check(ApplyUserPreset(cfg, UserPreset::kSharper));
    check(!cfg.comfort_vignette);
    check(!cfg.snap_turn && cfg.smooth_turn);
    check(cfg.render_scale_percent == 130 && cfg.aa_mode == 1);
  }

  // Match my PC: eye scale and AA only; comfort and turning stay untouched.
  {
    Config cfg = FactorySafeDefaults();
    cfg.comfort_vignette = true;
    cfg.vignette_strength = 0.9f;
    cfg.snap_turn = true;
    cfg.snap_turn_degrees = 60;
    cfg.smooth_turn = false;
    cfg.fov_scale = 0.8f;
    ApplyMatchPcPreset(cfg, HardwareTier::kPerformance);
    check(cfg.comfort_vignette && cfg.vignette_strength == 0.9f);
    check(cfg.snap_turn && cfg.snap_turn_degrees == 60 && !cfg.smooth_turn);
    check(cfg.fov_scale == 0.8f);
    check(cfg.render_scale_percent == 80 && cfg.aa_mode == 1);
    ApplyMatchPcPreset(cfg, HardwareTier::kQuality);
    check(cfg.render_scale_percent == 130);
    check(cfg.comfort_vignette && cfg.vignette_strength == 0.9f);
  }

  // The hardware-fit id is not a style preset and must not silently no-op.
  {
    Config cfg = FactorySafeDefaults();
    const int before = cfg.render_scale_percent;
    check(!ApplyUserPreset(cfg, UserPreset::kMatchPc));
    check(cfg.render_scale_percent == before);
  }

  if (!failures) std::printf("user_presets: checks passed\n");
  return failures ? 1 : 0;
}
