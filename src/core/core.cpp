// DSVR core startup: pinned executable/config gates, diagnostics and native
// adapters. Gameplay hooks wait for the startup-selected host's first accepted
// game frame; loading this DLL alone never starts XR or gameplay adapters.

#include <windows.h>

#include <atomic>
#include <string>

#include "../common/config.h"
#include "../common/logging.h"
#include "../common/version.h"
#include "camhook.h"
#include "core.h"
#include "frame_rate.h"
#include "crash.h"
#include "diagnostics.h"
#include "gate.h"
#include "presence.h"
#include "body_visibility.h"
#include "render.h"
#include "xinput_bridge.h"
#include "vr_trace.h"
#include "lab_runner.h"
#include "calibration.h"
#include "weapon_grip.h"
#include "native_ui.h"
#include "motion_blur.h"

namespace {
std::atomic<bool> g_started{false};
std::atomic<bool> g_gameplay_pending{false};
std::atomic<bool> g_desktop_blur_pending{false};
std::atomic<bool> g_desktop_frame_seen{false};
dsvr::Config g_startup_config;

std::string ReadOpenXRRuntime() {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Khronos\\OpenXR\\1", 0,
                    KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
    return "(no OpenXR runtime registered)";
  wchar_t buf[1024]{};
  DWORD bytes = sizeof(buf) - sizeof(wchar_t);
  DWORD type = 0;
  LSTATUS rc = RegQueryValueExW(key, L"ActiveRuntime", nullptr, &type,
                                (LPBYTE)buf, &bytes);
  RegCloseKey(key);
  if (rc != ERROR_SUCCESS || type != REG_SZ) return "(ActiveRuntime unreadable)";
  return dsvr::Utf8FromWide(buf);
}

}  // namespace

void dsvr::CoreOnRenderFrame() {
  if (!g_gameplay_pending.exchange(false, std::memory_order_acq_rel)) return;
  // A real accepted game frame exists. Do not patch native gameplay code while
  // the executable is still initializing on the loader's startup thread.
  const auto& config = g_startup_config;
  CalibrationStart(ConfigPathNextToExe() + L".calibration");
  WeaponGripsInitialize(GameExeDirectory() + L"\\DSVR-grips.txt");
  CameraHookInstall(config.vr_enabled);
  PresenceHookInstall(config.vr_enabled);
  BodyVisibilityHookInstall(config.vr_enabled);
  XInputBridgeInstall(config.vr_enabled && config.xr_input_enabled);
  FrameRateHookInstall(config.vr_enabled);
  NativeUiHookInstall(config.vr_enabled);
  MotionBlurHookInstall(true);
  VrTraceInstall(config.vr_enabled && config.verbose_logging);
  DSVR_LOG("Core: gameplay adapters activated on accepted render frame");
}

void dsvr::CoreOnDesktopFrame() {
  // Latch a qualifying Present even if it raced the installer's return. The
  // startup thread rechecks after publishing permission; no readiness is lost.
  g_desktop_frame_seen.store(true, std::memory_order_release);
  // Desktop readiness grants blur only, never XR/gameplay permissions. Consume
  // before installation so duplicate/reentrant frames cannot retry a partial pair.
  if (!g_desktop_blur_pending.exchange(false, std::memory_order_acq_rel)) return;
  const bool blur = MotionBlurHookInstall(true);
  DSVR_LOG("Core: desktop frame ready; motion_blur=%d; XR/gameplay remain disabled",
           (int)blur);
}

// Loader entry point. 0 = gate passed, non-zero = refused (game continues
// vanilla). Never throws, never shows UI (the game may be fullscreen).
extern "C" __declspec(dllexport) unsigned long __stdcall DsvrCoreStart() {
  bool expected = false;
  if (!g_started.compare_exchange_strong(expected, true)) return 0;

  std::wstring log_path = dsvr::LogPathNextToModule(nullptr, L"DSVR.log");
  dsvr::LogInit(log_path.c_str());
  dsvr::CrashHandlerInstall();

  DSVR_LOG("Core: DSVR %s starting", dsvr::kModVersion);
  DSVR_LOG("Core: WARNING: offline play only while DSVR is installed.");
  DSVR_LOG("Core: OpenXR runtime: %s", ReadOpenXRRuntime().c_str());

  if (!dsvr::GateCheckPinnedBuild()) {
    DSVR_ERROR("Core: version gate refused; game continues vanilla (no hooks installed)");
    return 1;
  }

  std::wstring cfg_path = dsvr::ConfigPathNextToExe();
  dsvr::Config config;
  if (!dsvr::ConfigLoad(cfg_path.c_str(), config)) {
    DSVR_LOG("Core: no config found; writing defaults %ls", cfg_path.c_str());
    dsvr::ConfigSaveDefaults(cfg_path.c_str());
    dsvr::ConfigLoad(cfg_path.c_str(), config);
  }
  dsvr::ConfigRestrictObserver(config);
  DSVR_LOG("Core: config vr_enabled=%d render_scale=%d motion_attacks=%d "
           "haptics=%d hand=%s style=%s chain='%s' xr_input=%d "
           "hide_second_model=%d",
           (int)config.vr_enabled, config.render_scale_percent,
           (int)config.motion_attacks, (int)config.haptics,
           config.dominant_hand_left ? "left" : "right",
           config.play_style.c_str(), config.loader_chain_proxy.c_str(),
           (int)config.xr_input_enabled, (int)config.hide_second_model);
  DSVR_LOG("Core: frame_limit_fps=%d is the pre-session fallback%s; the "
           "OpenXR runtime rate takes over when a session is live (see "
           "RateMatch/RateEvidence log lines). Native rate application is "
           "checked at the presentation owner; use measured game FPS for results.",
           config.frame_limit_fps,
           config.frame_limit_fps >= 120 ? " (POSSIBLY UNSAFE above 90)" : "");
  if (!config.vr_enabled && !config.observer_only)
    DSVR_LOG("Core: VR is DISABLED (flatscreen-safe). Enable it in "
             "DSVR-Setup, then restart the game.");
  if (!config.offline_only_ack)
    DSVR_WARN("Core: offline_only_ack is false; run DSVR-Setup and confirm "
               "the offline warning so this stops nagging (log only).");

  // Desktop observation is a distinct startup path, not VR with a few options
  // disabled. Never install the body/pad/lab/render adapters or publish a frame
  // host here: a late add-on callback or F7 must not acquire XR/gameplay rights.
  if (config.observer_only) {
    dsvr::DiagnosticsStart(config, cfg_path);
    DSVR_LOG("Core: OBSERVER ONLY armed; %s; "
              "no XR, gameplay writes, "
             "injected input or render host; restart required to change mode. "
              "This does not isolate saves or enforce offline play.",
              config.combat_trace_only ? "automatic ten-minute combat recording after startup; no F9 required" :
                                        "F9 installs observation hooks after startup");
    return 0;
  }

  dsvr::LabInitialize(true);  // reached only after the pinned executable gate
  dsvr::DiagnosticsStart(config, cfg_path);

  // Publish immutable adapter permissions only after installing the selected
  // frame host. A callback racing this installation cannot consume them early.
  g_startup_config = config;
  const bool host_ready = dsvr::RenderHookInstall(
      config.vr_enabled, config.verbose_logging, config.reshade_host);
  g_gameplay_pending.store(config.vr_enabled && host_ready, std::memory_order_release);
  g_desktop_blur_pending.store(!config.vr_enabled && host_ready,
                               std::memory_order_release);
  if (!config.vr_enabled && host_ready &&
      g_desktop_frame_seen.load(std::memory_order_acquire))
    dsvr::CoreOnDesktopFrame();

  DSVR_LOG("Core: frame host installed=%d; gameplay waits for accepted frame "
           "(F9=log marker, F7=reload config)", (int)host_ready);
  return 0;
}

BOOL APIENTRY DllMain(HMODULE /*self*/, DWORD reason, LPVOID /*reserved*/) {
  if (reason == DLL_PROCESS_ATTACH) {
    // Startup runs outside DllMain; gameplay hooks wait for an accepted frame.
  }
  return TRUE;
}
