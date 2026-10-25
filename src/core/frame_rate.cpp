#include "frame_rate.h"

#include <windows.h>

#include <MinHook.h>

#include <chrono>
#include <cstdint>

#include "../common/logging.h"
#include "../common/memory.h"
#include "diagnostics.h"
#include "display_rate.h"
#include "frame_pacing.h"
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

// The native presentation owner's full arity is not pinned: the traced callsite
// passes at least a fifth (stack) argument. Forward every argument the detour
// receives so the hook cannot silently drop one the native callee still reads.
using PresentOwnerFn =
    std::uint64_t(__fastcall*)(void*, void*, int, void*, std::uintptr_t);
using RateSetterFn = void(__fastcall*)(void*, int, unsigned char);
PresentOwnerFn g_orig = nullptr;
void* g_owner = nullptr;

// Pacing evidence (frame_pacing.h): the wall interval between successive native
// frame boundaries. This detour is the only writer and runs on the game's
// render thread, so no lock is needed. It measures the game's submission
// cadence; the XR-reported refresh (display_rate.h) is the display's cadence.
// K1 needs p50/p95/p99 and drop counts, which a mean/max pair cannot give.
FramePacingTracker g_pacing;
std::chrono::steady_clock::time_point g_last_boundary{};
std::chrono::steady_clock::time_point g_pacing_logged{};
constexpr auto kPacingLogInterval = std::chrono::seconds(5);

void RecordPacingBoundary() {
  const auto now = std::chrono::steady_clock::now();
  const bool first = g_last_boundary.time_since_epoch().count() == 0;
  if (!first) {
    g_pacing.Record(
        std::chrono::duration<double, std::milli>(now - g_last_boundary).count());
    if (now - g_pacing_logged >= kPacingLogInterval) {
      g_pacing_logged = now;
      const PacingWindow window = g_pacing.TakeInterval();
      EffectiveRate rate{};
      const int target = XrEffectiveRate(rate) ? rate.effective_fps : 0;
      DSVR_LOG("Pacing: frames=%llu mean=%.3fms (%.2fHz) p50=%.3f p95=%.3f p99=%.3f "
               "min=%.3f max=%.3f long=%llu short=%llu rejected=%llu even=%.3f "
               "target=%d%s",
               (unsigned long long)window.frames, window.mean_ms, window.MeanHz(),
               window.p50_ms, window.p95_ms, window.p99_ms, window.min_ms,
               window.max_ms, (unsigned long long)window.long_frames,
               (unsigned long long)window.short_frames,
               (unsigned long long)window.rejected, window.Evenness(), target,
               target && !PacingMatchesTarget(window, target)
                   ? " NOT-MATCHING-RUNTIME-CADENCE" : "");
    }
  }
  g_last_boundary = now;
  if (first) g_pacing_logged = now;
}

std::uint64_t __fastcall PresentOwnerDetour(void* owner, void* arg2, int arg3,
                                            void* arg4, std::uintptr_t arg5) {
  CpuStageScope boundary_time(CpuStage::NativeFrameBoundary);
  RecordPacingBoundary();
  if (owner && owner != g_owner) {
    g_pacing.Reset();
    g_last_boundary = {};
    g_owner = owner;
    DSVR_LOG("FrameRate: presentation owner captured %p", owner);
  }
  if (owner && XrSessionRunning()) {
    EffectiveRate rate{};
    if (XrEffectiveRate(rate)) {
      int current = 0;
      unsigned char flag = 0;
      if (GuardedRead((std::uintptr_t)owner + 0x60, current) &&
          ShouldApplyNativeRate(current, rate.effective_fps) &&
          GuardedRead((std::uintptr_t)owner + 0x68, flag)) {
        auto setter = (RateSetterFn)(ModuleBase(nullptr) + kRateSetterRva);
        setter(owner, rate.effective_fps, flag);
        DSVR_LOG("FrameRate: applied effective=%d (native rate was %d, "
                 "source=%s runtime=%.3fHz requested=%d) via native setter",
                 rate.effective_fps, current,
                 rate.from_runtime ? "runtime" : "fallback", rate.runtime_hz,
                 rate.requested_fps);
      }
    }
  }
  return g_orig(owner, arg2, arg3, arg4, arg5);
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
    const auto target = (void*)(base + kPresentOwnerRva);
    MH_DisableHook(target);
    MH_RemoveHook(target);
    g_orig = nullptr;
  }
  g_owner = nullptr;
  g_pacing.Reset();
  g_last_boundary = {};
  g_pacing_logged = {};
}

}  // namespace dsvr
