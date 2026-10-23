// Real config writer/reader + diagnostic thread/reload, without game or XR.
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <string>

#include "../src/common/config.h"
#include "../src/core/diagnostics.h"
#include "../src/core/lab_runner.h"
#include "../src/core/vr_trace.h"
#include "../src/core/xr.h"

#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); \
  return 1; } } while (0)

namespace {
std::atomic<unsigned> xr_calls{0}, lab_calls{0}, camera_calls{0}, trace_calls{0}, captures{0};
bool refuse_camera = false;
}
namespace dsvr {
void ModMenuToggle() {}
void CalibrationRecenter() {}
void XrToggleSanctuary() { ++xr_calls; }
void XrRecenterSanctuary() { ++xr_calls; }
void LabPump() { ++lab_calls; }
void LabRequestAbort() { ++lab_calls; }
bool CameraHookInstall(bool vr, bool observer) { ++camera_calls; return !vr && observer && !refuse_camera; }
bool VrTraceInstall(bool enabled) { ++trace_calls; return enabled; }
bool VrTraceBegin(const wchar_t*, unsigned) { ++captures; return true; }
void VrTracePump() {}
void VrTraceFinish(bool) {}
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
  const bool observer = std::string(argv[1]).starts_with("observer");
  const bool disabled = std::string(argv[1]) == "disabled";
  refuse_camera = std::string(argv[1]) == "observer-refused";
  CHECK(!DiagnosticsBeginCapture());
  wchar_t temp[MAX_PATH]{}, path[MAX_PATH]{};
  CHECK(GetTempPathW(MAX_PATH, temp) && GetTempFileNameW(temp, L"DSV", 0, path));
  Config requested;
  requested.observer_only = observer;
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
  CHECK(camera_calls == 0 && trace_calls == 0 && captures == 0);
  CHECK(DiagnosticsBeginCapture() == !refuse_camera);
  CHECK(DiagnosticsBeginCapture() == !refuse_camera);
  CHECK(camera_calls == (observer ? 1u : 0u));
  CHECK(trace_calls == (observer && !refuse_camera ? 1u : 0u));
  CHECK(captures == (refuse_camera ? 0u : 2u));
  auto c = DiagnosticsCurrentConfig();
  CHECK(c.observer_only == observer && c.vr_enabled == (!observer && !disabled));
  requested.observer_only = !observer;  // F7 must not change the startup mode
  requested.render_scale_percent = 125;
  CHECK(ConfigSave(path, requested));
  CHECK(DiagnosticsReloadConfig());
  c = DiagnosticsCurrentConfig();
  CHECK(c.observer_only == observer && c.render_scale_percent == 125);
  CHECK(c.vr_enabled == (!observer && !disabled) && c.headlook_enabled == !observer);
  CHECK(c.xr_input_enabled == !observer && c.arm_ik_enabled == !observer);
  CHECK(c.arm_ik_log_only == observer && c.motion_attacks == !observer);
  CHECK(c.haptics == !observer && c.sanctuary_override == !observer);
  CHECK(c.hide_player_model == !observer && c.hide_second_model == !observer);
  DiagnosticsStart(requested, L"must-not-replace-startup-path");
  CHECK(DiagnosticsCurrentConfig().observer_only == observer);
  CHECK(DiagnosticsReloadConfig());  // retained original config path

  Config applied=DiagnosticsCurrentConfig();
  applied.render_scale_percent=133;
  applied.eye_height_override=true;applied.eye_height=1.82f;
  CHECK(DiagnosticsRequestConfig(applied,false));
  CHECK(WaitForSettingsStatus("Settings applied for this session."));
  CHECK(DiagnosticsCurrentConfig().render_scale_percent==133);
  Config on_disk{};
  CHECK(ConfigLoad(path,on_disk) && on_disk.render_scale_percent==125);
  auto state=DiagnosticsSettingsSnapshot();
  CHECK(!state.busy && state.applied.render_scale_percent==133 && state.saved.render_scale_percent==125);
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
  const bool save_failed=WaitForSettingsStatus("Could not save settings. Previous settings remain active.");
  CloseHandle(prevent_replace);
  CHECK(save_failed);
  CHECK(DiagnosticsCurrentConfig().render_scale_percent==143);
  CHECK(ConfigLoad(path,on_disk) && on_disk.render_scale_percent==143);
  CHECK(DeleteFileW(path));
  CHECK(!DiagnosticsReloadConfig());
  CHECK(DiagnosticsCurrentConfig().observer_only == observer);
  CHECK(DiagnosticsCurrentConfig().render_scale_percent == 143);
  DiagnosticsStop();
  CHECK(xr_calls == 0);
  if (observer) CHECK(lab_calls == 0);
  std::printf("test_diagnostics_mode (%s): PASS\n", argv[1]);
  return 0;
}
