#pragma once

#include <cstddef>
#include <cstring>

#include "../common/config.h"
#include "perf_fit.h"

namespace dsvr {

// One user-facing preset. A preset owns a small, predictable set of fields and
// preserves everything else. Frame rate is never a preset: it stays
// headset/monitor-owned through DSR-FPS-Unlock.
enum class UserPreset { kComfort, kBalanced, kSharper, kMatchPc };

struct UserPresetInfo {
  UserPreset id;
  const char* verb;   // stable verb suffix: `preset-<verb>`
  const char* label;  // what the player sees
  const char* detail; // one line for the launcher and the native setup UI
  bool hardware_fit;  // true = eye scale and AA only, chosen from the hardware
};

inline constexpr UserPresetInfo kUserPresets[] = {
    {UserPreset::kComfort, "comfort", "Comfort",
     "Soft edges, snap turning, 90% eye scale. Start here if VR makes you queasy.",
     false},
    {UserPreset::kBalanced, "balanced", "Balanced",
     "Recommended. Mild vignette, smooth turning, full eye scale.", false},
    {UserPreset::kSharper, "sharper", "Sharper",
     "Clearest picture: no vignette, 130% eye scale. Needs GPU headroom.",
     false},
    {UserPreset::kMatchPc, "match", "Match my PC",
     "Reads the hardware and picks a safe eye scale and AA. Comfort stays as it is.",
     true},
};
inline constexpr std::size_t kUserPresetCount =
    sizeof(kUserPresets) / sizeof(kUserPresets[0]);

inline const UserPresetInfo* FindUserPreset(const char* verb) {
  if (!verb) return nullptr;
  for (std::size_t i = 0; i < kUserPresetCount; ++i)
    if (std::strcmp(kUserPresets[i].verb, verb) == 0) return &kUserPresets[i];
  return nullptr;
}

// Style presets. Returns false for the hardware-fit preset, which the caller
// applies with ApplyMatchPcPreset once the hardware tier is probed.
inline bool ApplyUserPreset(Config& cfg, UserPreset preset) {
  switch (preset) {
    case UserPreset::kComfort:
      cfg.comfort_vignette = true;
      cfg.vignette_strength = 0.85f;
      cfg.fov_scale = 0.9f;
      cfg.snap_turn = true;
      cfg.snap_turn_degrees = 30;
      cfg.smooth_turn = false;
      cfg.render_scale_percent = 90;
      cfg.aa_mode = 1;
      return true;
    case UserPreset::kBalanced:
      cfg.comfort_vignette = true;
      cfg.vignette_strength = 0.65f;
      cfg.fov_scale = 1.0f;
      cfg.snap_turn = false;
      cfg.smooth_turn = true;
      cfg.smooth_turn_speed_dps = 180.0f;
      cfg.render_scale_percent = 100;
      cfg.aa_mode = 1;
      return true;
    case UserPreset::kSharper:
      cfg.comfort_vignette = false;
      cfg.fov_scale = 1.0f;
      cfg.snap_turn = false;
      cfg.smooth_turn = true;
      cfg.render_scale_percent = 130;
      cfg.aa_mode = 1;
      return true;
    case UserPreset::kMatchPc:
      break;
  }
  return false;
}

// Hardware fit: eye-buffer scale and AA only. A hardware read must never flip
// the player's comfort or turning choices.
inline void ApplyMatchPcPreset(Config& cfg, HardwareTier tier) {
  const AutoFit fit = AutoFitForTier(tier);
  cfg.render_scale_percent = fit.render_scale_percent;
  cfg.aa_mode = fit.aa_mode;
}

}  // namespace dsvr
