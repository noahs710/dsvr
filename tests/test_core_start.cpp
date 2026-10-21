// Exercise the real startup entry point with recording subsystem boundaries.
// No game, config/install mutation, D3D device or XR runtime is involved.
#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include "../src/common/config.h"
#include "../src/common/logging.h"
#include "../src/core/camhook.h"
#include "../src/core/core.h"
#include "../src/core/diagnostics.h"
#include "../src/core/gate.h"
#include "../src/core/crash.h"
#include "../src/core/lab_runner.h"
#include "../src/core/presence.h"
#include "../src/core/body_visibility.h"
#include "../src/core/render.h"
#include "../src/core/vr_trace.h"
#include "../src/core/xinput_bridge.h"

extern "C" unsigned long __stdcall DsvrCoreStart();

#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); \
  return 1; } } while (0)

namespace {
dsvr::Config requested, published;
bool pinned = true, host_ok = true, blur_ok = true, early_desktop = false;
std::vector<std::string> calls;
void Record(const char* name, bool enabled) {
  calls.emplace_back(std::string(name) + (enabled ? ":on" : ":off"));
}
}

namespace dsvr {
bool NativeUiHookInstall(bool enabled) {Record("native-ui",enabled);return enabled;}
bool MotionBlurHookInstall(bool enabled) {
  Record("blur",enabled);
  CoreOnDesktopFrame();  // reentry must not retry even a partially failed pair
  CoreOnRenderFrame();
  return enabled && blur_ok;
}
void CalibrationStart(const std::wstring&) {}
bool WeaponGripsInitialize(const std::wstring&) { return true; }
std::wstring GameExeDirectory() { return L"unused"; }
bool LogInit(const wchar_t*) { return true; }
void LogWrite(LogLevel, const char*, int, const char*, ...) {}
std::wstring LogPathNextToModule(void*, const wchar_t*) { return L"unused"; }
std::string Utf8FromWide(const std::wstring&) { return "unused"; }
void CrashHandlerInstall() {}
bool GateCheckPinnedBuild() { return pinned; }
std::wstring ConfigPathNextToExe() { return L"unused"; }
bool ConfigLoad(const wchar_t*, Config& out) { out = requested; return true; }
bool ConfigSaveDefaults(const wchar_t*) { return false; }
void DiagnosticsStart(const Config& c, std::wstring) {
  published = c;
  calls.emplace_back("diagnostics");
}
void LabInitialize(bool enabled) { Record("lab", enabled); }
bool CameraHookInstall(bool enabled, bool observer) {
  if (observer) calls.emplace_back("camera-observer");
  else Record("camera", enabled);
  CoreOnRenderFrame();  // reentrant readiness cannot acquire the adapters twice
  return enabled || observer;
}
bool PresenceHookInstall(bool enabled) { Record("presence", enabled); return enabled; }
bool BodyVisibilityHookInstall(bool enabled) {Record("body-geometry",enabled);return enabled;}
bool XInputBridgeInstall(bool enabled) { Record("input", enabled); return enabled; }
bool FrameRateHookInstall(bool enabled) { Record("framerate", enabled); return enabled; }
bool VrTraceInstall(bool enabled) { Record("trace", enabled); return enabled; }
bool RenderHookInstall(bool enabled, bool, bool) {
  Record("render", enabled);
  CoreOnRenderFrame();  // a callback during host installation must do nothing
  if (early_desktop) CoreOnDesktopFrame();
  return host_ok;  // false VR requests install only a desktop observer
}
}

int main(int argc, char** argv) {
  CHECK(argc == 2);
  const std::string mode = argv[1];
  const bool disabled = mode.starts_with("disabled");
  early_desktop = mode == "disabled-earlyframe";
  requested.vr_enabled = !disabled;
  requested.observer_only = mode == "observer";
  requested.verbose_logging = false;  // observer must work independently
  requested.headlook_enabled = true;
  requested.xr_input_enabled = true;
  requested.arm_ik_enabled = true;
  requested.arm_ik_log_only = false;
  requested.hide_player_model = true;
  requested.hide_second_model = true;
  requested.haptics = true;
  requested.motion_attacks = true;
  pinned = mode != "refused";
  host_ok = mode != "nohost" && mode != "disabled-nohost";
  blur_ok = mode != "disabled-blur-failed";
  const auto result = DsvrCoreStart();
  CHECK(result == (pinned ? 0u : 1u));
  if (!pinned) {
    CHECK(calls.empty());
  } else if (requested.observer_only) {
    // No call, even with false, to the render/presence/pad/lab installers.
    // Camera observation gets its own immutable permission (added below).
    CHECK((calls == std::vector<std::string>{"diagnostics"}));
    CHECK(published.observer_only);
    CHECK(!published.vr_enabled && !published.headlook_enabled);
    CHECK(!published.xr_input_enabled && !published.motion_attacks && !published.haptics);
    CHECK(!published.arm_ik_enabled && published.arm_ik_log_only);
    CHECK(!published.hide_player_model && !published.hide_second_model);
  } else {
    std::vector<std::string> expected{"lab:on", "diagnostics",
      requested.vr_enabled ? "render:on" : "render:off"};
    if (early_desktop && host_ok) expected.emplace_back("blur:on");
    CHECK(calls == expected);
  }
  const auto startup_only = calls;
  dsvr::CoreOnRenderFrame();
  if (pinned && !requested.observer_only && requested.vr_enabled && host_ok) {
    CHECK((calls == std::vector<std::string>{"lab:on", "diagnostics", "render:on",
      "camera:on", "presence:on", "body-geometry:on", "input:on", "framerate:on", "native-ui:on", "blur:on", "trace:off"}));
  } else {
    CHECK(calls == startup_only);
  }
  dsvr::CoreOnDesktopFrame();
  if (pinned && !requested.observer_only && disabled && host_ok) {
    auto expected = startup_only;
    if (!early_desktop) expected.emplace_back("blur:on");
    CHECK(calls == expected);  // blur only, regardless of install success/failure
  } else if (!requested.vr_enabled || requested.observer_only || !pinned) {
    CHECK(calls == startup_only);
  }
  const auto once = calls;
  dsvr::CoreOnDesktopFrame();
  dsvr::CoreOnRenderFrame();
  CHECK(DsvrCoreStart() == 0);
  CHECK(calls == once);  // duplicate loader entry cannot acquire new rights
  std::printf("test_core_start (%s): PASS\n", mode.c_str());
  return 0;
}
