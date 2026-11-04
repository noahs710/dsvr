#pragma once
// Hardware-tier fit for the launcher's "Match my PC" preset. It chooses the
// eye-buffer scale and antialiasing only. Frame rate always comes from the
// active headset or monitor and is delegated to DSR-FPS-Unlock. Unknown
// hardware fails closed to the lowest scale tier.

namespace dsvr {

enum class HardwareTier { kPerformance = 0, kBalanced = 1, kQuality = 2 };

// What the launcher probes: usable system RAM (GB), dedicated GPU VRAM (GB,
// 0 = unknown/integrated), and whether a discrete GPU is present.
struct HardwareSpec {
  int system_ram_gb = 0;
  int gpu_vram_gb = 0;
  bool discrete_gpu = false;
};

inline HardwareTier TierForHardware(const HardwareSpec& spec) {
  if (spec.system_ram_gb >= 32 && spec.gpu_vram_gb >= 12)
    return HardwareTier::kQuality;
  if (spec.system_ram_gb >= 16 && spec.gpu_vram_gb >= 8)
    return HardwareTier::kBalanced;
  // A discrete GPU with system RAM at the floor qualifies for Balanced even
  // when VRAM is unknown (older discrete parts under-report); thin system RAM
  // fails closed regardless of GPU. Unknown everything stays Performance.
  if (spec.discrete_gpu && spec.system_ram_gb >= 8)
    return HardwareTier::kBalanced;
  if (spec.system_ram_gb >= 8 && spec.gpu_vram_gb >= 8)
    return HardwareTier::kBalanced;
  return HardwareTier::kPerformance;
}

// What "Match my PC" writes: eye-buffer scale and AA only. AA stays inside the
// modes the completed image supports (0 = off, 1 = FXAA); it must never write a
// reserved MSAA/TAA value that ConfigValid would reject.
struct AutoFit {
  int render_scale_percent = 100;
  int aa_mode = 1;
};

inline AutoFit AutoFitForTier(HardwareTier tier) {
  switch (tier) {
    case HardwareTier::kQuality:
      return {130, 1};
    case HardwareTier::kBalanced:
      return {100, 1};
    case HardwareTier::kPerformance:
      break;
  }
  return {80, 1};
}

}  // namespace dsvr
