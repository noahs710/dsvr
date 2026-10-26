// Real config writer/reader + diagnostic thread/reload, without game or XR.
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <string>

#include "../src/common/config.h"
#include "../src/common/logging.h"
#include "../src/core/diagnostics.h"
#include "../src/core/lab_runner.h"
#include "../src/core/vr_trace.h"
#include "../src/core/xr.h"
#include "../src/core/combat_native.h"

#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); \
  return 1; } } while (0)

namespace {
std::atomic<unsigned> xr_calls{0}, lab_calls{0}, camera_calls{0}, trace_calls{0}, captures{0};
bool refuse_camera = false;
bool combat_install=false;
std::atomic<unsigned> capture_duration{0};
std::atomic<bool> player_loaded{false};
}
namespace dsvr {
CombatNativeStatus CombatNativeGetStatus() { return {}; }
bool CombatNativeLocalPlayer(std::uintptr_t& player) {
  player=player_loaded ? 0x10000 : 0;return player!=0;
}
void ModMenuToggle() {}
void CalibrationRecenter() {}
void XrToggleSanctuary() { ++xr_calls; }
void XrRecenterSanctuary() { ++xr_calls; }
void LabPump() { ++lab_calls; }
void LabRequestAbort() { ++lab_calls; }
bool CameraHookInstall(bool vr, bool observer) { ++camera_calls; return !vr && observer && !refuse_camera; }
bool VrTraceInstall(bool enabled) { ++trace_calls; return enabled; }
bool VrTraceInstallCombat() { ++trace_calls;combat_install=true;return true; }
bool VrTraceBegin(const wchar_t*, unsigned duration) { capture_duration=duration; ++captures; return true; }
void VrTracePump() {}
void VrTraceFinish(bool) {}
bool VrTraceConsumeStopRequest(const wchar_t*) { return false; }
TraceCaptureState VrTraceState() { return TraceCaptureState::Complete; }
}

namespace {
bool WaitForSettingsStatus(const char* expected) {
  for (int i=0;i<300;++i) {
    if (dsvr::DiagnosticsSettingsStatus()==expected) return true;
    Sleep(10);
  }
  return false;
}
}

int main(int argc, char** argv) {
  using namespace dsvr;
  CHECK(argc == 2);
  const bool combat=std::string(argv[1])=="combat";
  const bool observer = combat || std::string(argv[1]).starts_with("observer");
  const bool disabled = std::string(argv[1]) == "disabled";
  refuse_camera = std::string(argv[1]) == "observer-refused";
  CHECK(!DiagnosticsBeginCapture());
  wchar_t temp[MAX_PATH]{}, path[MAX_PATH]{};
  CHECK(GetTempPathW(MAX_PATH, temp) && GetTempFileNameW(temp, L"DSV", 0, path));
  Config requested;
  requested.observer_only = observer;
  requested.combat_trace_only = combat;
  requested.vr_enabled = !disabled;
  requested.headlook_enabled = requested.xr_input_enabled = true;
  requested.hide_player_model = requested.hide_second_model = true;
  requested.motion_attacks = requested.haptics = requested.arm_ik_enabled = true;
  requested.arm_ik_log_only = false;
  requested.sanctuary_override = true;
  requested.hotkeys_enabled = false;  // never consume real desktop keys
  requested.verbose_logging = true;
  CHECK(ConfigSave(path, requested));
  DiagnosticsStart(requested, path);
  if(combat) {
    Sleep(60);CHECK(captures==0 && trace_calls==0);
    player_loaded=true;
    for(unsigned i=0;i<100 && captures==0;++i) Sleep(10);
    CHECK(captures==1 && capture_duration==600000);
    Sleep(120); // a completed capture must not automatically start another
    CHECK(captures==1);
  } else {
    CHECK(camera_calls == 0 && trace_calls == 0 && captures == 0);
    CHECK(DiagnosticsBeginCapture() == !refuse_camera);
    CHECK(DiagnosticsBeginCapture() == !refuse_camera);
  }
  CHECK(camera_calls == (observer && !combat ? 1u : 0u));
  CHECK(combat_install==combat);
  CHECK(trace_calls == (observer && !refuse_camera ? 1u : 0u));
  CHECK(captures == (refuse_camera ? 0u : combat ? 1u : 2u));
  auto c = DiagnosticsCurrentConfig();
  CHECK(c.observer_only == observer && c.vr_enabled == (!observer && !disabled));
  requested.observer_only = !observer;  // F7 must not change the startup mode
  requested.combat_trace_only = !combat;
  requested.render_scale_percent = 125;
  CHECK(ConfigSave(path, requested));
  CHECK(DiagnosticsReloadConfig());
  c = DiagnosticsCurrentConfig();
  CHECK(c.observer_only == observer && c.render_scale_percent == 125);
  CHECK(c.combat_trace_only==combat);
  CHECK(c.vr_enabled == (!observer && !disabled) && c.headlook_enabled == !observer);
  CHECK(c.xr_input_enabled == !observer && c.arm_ik_enabled == !observer);
  CHECK(c.arm_ik_log_only == observer && c.motion_attacks == !observer);
  CHECK(c.haptics == !observer && c.sanctuary_override == !observer);
  CHECK(c.hide_player_model == !observer && c.hide_second_model == !observer);
  DiagnosticsStart(requested, L"must-not-replace-startup-path");
  CHECK(DiagnosticsCurrentConfig().observer_only == observer);
  CHECK(DiagnosticsReloadConfig());  // retained original config path

  Config applied=DiagnosticsCurrentConfig();
  for(int scale:{126,127,128}) {
    auto rapid=applied;rapid.render_scale_percent=scale;
    CHECK(DiagnosticsRequestConfig(rapid,true));
    CHECK(DiagnosticsCurrentConfig().render_scale_percent==scale);
  }
  CHECK(WaitForSettingsStatus("Settings saved."));
  Config coalesced{};
  CHECK(ConfigLoad(path,coalesced) && coalesced.render_scale_percent==128);
  applied.render_scale_percent=133;
  applied.eye_height_override=true;applied.eye_height=1.82f;
  CHECK(DiagnosticsRequestConfig(applied,false));
  CHECK(WaitForSettingsStatus("Settings applied for this session."));
  CHECK(DiagnosticsCurrentConfig().render_scale_percent==133);
  Config on_disk{};
  CHECK(ConfigLoad(path,on_disk) && on_disk.render_scale_percent==128);
  auto state=DiagnosticsSettingsSnapshot();
  CHECK(!state.busy && state.applied.render_scale_percent==133 && state.saved.render_scale_percent==128);
  // Apply -> test -> Save must persist the exact same candidate, not force an
  // additional edit to re-enable Save. Save failures must retain this receipt.
  CHECK(DiagnosticsRequestConfig(applied,true));
  CHECK(WaitForSettingsStatus("Settings saved."));
  CHECK(ConfigLoad(path,on_disk) && on_disk.render_scale_percent==133);
  CHECK(on_disk.eye_height_override && on_disk.eye_height==1.82f);
  state=DiagnosticsSettingsSnapshot();CHECK(!state.busy && state.applied==state.saved);

  Config saved=applied;
  saved.render_scale_percent=143;
  CHECK(DiagnosticsRequestConfig(saved,true));
  CHECK(WaitForSettingsStatus("Settings saved."));
  CHECK(DiagnosticsCurrentConfig().render_scale_percent==143);
  CHECK(ConfigLoad(path,on_disk) && on_disk.render_scale_percent==143);

  HANDLE prevent_replace=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,
      OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  CHECK(prevent_replace!=INVALID_HANDLE_VALUE);
  Config failed_save=saved;
  failed_save.render_scale_percent=151;
  CHECK(DiagnosticsRequestConfig(failed_save,true));
  const bool save_failed=WaitForSettingsStatus("Settings applied, but could not be saved. Check folder permissions.");
  CloseHandle(prevent_replace);
  CHECK(save_failed);
  CHECK(DiagnosticsCurrentConfig().render_scale_percent==151);
  CHECK(ConfigLoad(path,on_disk) && on_disk.render_scale_percent==143);
  CHECK(DeleteFileW(path));
  CHECK(!DiagnosticsReloadConfig());
  CHECK(DiagnosticsCurrentConfig().observer_only == observer);
  CHECK(DiagnosticsCurrentConfig().render_scale_percent == 151);
  DiagnosticsStop();
  bool health_logged=false;
  for(const auto& entry:LogRecent())
    if(entry.text.find("CombatHealth:")!=std::string::npos &&
       entry.text.find("damage_binding=0")!=std::string::npos) health_logged=true;
  CHECK(health_logged);
  CHECK(xr_calls == 0);
  if (observer) CHECK(lab_calls == 0);
  std::printf("test_diagnostics_mode (%s): PASS\n", argv[1]);
  return 0;
}
