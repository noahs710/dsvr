#include "frame_rate.h"

#include <windows.h>

#include <MinHook.h>

#include <cstdint>

#include "../common/logging.h"
#include "../common/memory.h"
#include "diagnostics.h"
#include "display_rate.h"
#include "xr.h"
#include "cpu_frame_timer.h"

namespace dsvr {
namespace {

// Presentation owner (traced; see build/empryo-review/native-rate-static.json):
// first-frame reset + timed Present. Its RCX is the rate owner.
constexpr std::uintptr_t kPresentOwnerRva = 0xCE2DD0;
constexpr unsigned char kPresentOwnerPrologue[] = {0x48, 0x89, 0x5C, 0x24,
                                                    0x10, 0x57, 0x48, 0x83};
// Native rate setter: void(owner, rate:int, flag:byte). Writes owner+0x60
// and resets epoch/counters/history. Call only at a frame boundary.
constexpr std::uintptr_t kRateSetterRva = 0xCE2CF0;
constexpr unsigned char kRateSetterPrologue[] = {0x33, 0xC0, 0x89, 0x51,
                                                  0x60, 0x48, 0x89, 0x81};

using PresentOwnerFn = std::uint64_t(__fastcall*)(void*, void*, int, void*);
using RateSetterFn = void(__fastcall*)(void*, int, unsigned char);
PresentOwnerFn g_orig = nullptr;
void* g_owner = nullptr;
int g_applied_fps = 0;

std::uint64_t __fastcall PresentOwnerDetour(void* owner, void* arg2, int arg3,
                                   void* arg4) {
  CpuStageScope boundary_time(CpuStage::NativeFrameBoundary);
  if (owner && owner != g_owner) {
    g_owner = owner;
    g_applied_fps = 0;
    DSVR_LOG("FrameRate: presentation owner captured %p", owner);
  }
  if (owner && XrSessionRunning()) {
    EffectiveRate rate{};
    if (XrEffectiveRate(rate)) {
      int current = 0;
      unsigned char flag = 0;
      if (GuardedRead((std::uintptr_t)owner + 0x60, current) &&
          ShouldApplyNativeRate(current, rate.effective_fps, g_applied_fps) &&
          GuardedRead((std::uintptr_t)owner + 0x68, flag)) {
        auto setter = (RateSetterFn)(ModuleBase(nullptr) + kRateSetterRva);
        setter(owner, rate.effective_fps, flag);
        g_applied_fps = rate.effective_fps;
        DSVR_LOG("FrameRate: applied effective=%d (native rate was %d, "
                 "source=%s runtime=%.3fHz requested=%d) via native setter",
                 rate.effective_fps, current,
                 rate.from_runtime ? "runtime" : "fallback", rate.runtime_hz,
                 rate.requested_fps);
      }
    }
  }
  return g_orig(owner, arg2, arg3, arg4);
}

}  // namespace

bool FrameRateHookInstall(bool vr_enabled) {
  if (g_orig) return false;
  if (!vr_enabled) {
    DSVR_LOG("FrameRate: skipped (vr_enabled=false)");
    return false;
  }
  std::uintptr_t base = ModuleBase(nullptr);
  auto target = (void*)(base + kPresentOwnerRva);
  auto setter = (void*)(base + kRateSetterRva);
  if (!BytesMatch((std::uintptr_t)target, kPresentOwnerPrologue,
                  sizeof(kPresentOwnerPrologue)) ||
      !BytesMatch((std::uintptr_t)setter, kRateSetterPrologue,
                  sizeof(kRateSetterPrologue))) {
    DSVR_ERROR("FrameRate: prologue mismatch (owner/setter); refusing to hook");
    return false;
  }
  MH_STATUS init = MH_Initialize();
  if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
    DSVR_ERROR("FrameRate: MinHook initialize failed (%d)", (int)init);
    return false;
  }
  if (MH_CreateHook(target, (void*)&PresentOwnerDetour, (void**)&g_orig) !=
      MH_OK) {
    DSVR_ERROR("FrameRate: MH_CreateHook failed");
    return false;
  }
  if (MH_EnableHook(target) != MH_OK) {
    MH_RemoveHook(target);
    g_orig = nullptr;
    DSVR_ERROR("FrameRate: MH_EnableHook failed");
    return false;
  }
  DSVR_LOG("FrameRate: presentation-owner hook installed (RVA 0x%llx); "
           "runtime rate will be applied via the native setter",
           (unsigned long long)kPresentOwnerRva);
  return true;
}

void FrameRateHookUninstall() {
  if (g_orig) {
    std::uintptr_t base = ModuleBase(nullptr);
    MH_DisableHook((void*)(base + kPresentOwnerRva));
    g_orig = nullptr;
  }
}

}  // namespace dsvr
