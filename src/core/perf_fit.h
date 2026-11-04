#pragma once
// Hardware-tier presets choose eye-buffer scale and antialiasing only.
// Frame rate always comes from the active headset or monitor and is delegated
// to DSR-FPS-Unlock. Unknown hardware fails closed to the lowest scale tier.


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

// AA modes supported on the completed image DSVR receives. Quality-tier
// auto-smooth selects MSAA 4x; other values are reserved for a future
// native scene path and are never selected.
enum class AaMode {
  kOff = 0,
  kFxaa = 1,
  kMsaa2x = 2,
  kMsaa4x = 3,
  kTaa = 4,
};

// One launcher preset: stable id, eye-buffer scale and antialiasing.
// Rates are not part of a preset. Mono defaults to 100% eye scale, stereo 85%.
struct Preset {
  const char* id = "";
  int render_scale_percent = 100;
  AaMode aa_mode = AaMode::kFxaa;
};

inline int DefaultScaleForStereo(bool stereo_enabled) {
  return stereo_enabled ? 85 : 100;
}

inline Preset PresetForTier(HardwareTier tier, bool prefer_smooth) {
  Preset out{};
  out.aa_mode = AaMode::kFxaa;
  if (prefer_smooth) {
    out.id = "auto-smooth";
    out.render_scale_percent = 100;
    if (tier == HardwareTier::kQuality) out.aa_mode = AaMode::kMsaa4x;
  } else {
    out.id = "auto-res";
    out.render_scale_percent = tier == HardwareTier::kQuality ? 130 : 100;
  }
  return out;
}


}  // namespace dsvr
