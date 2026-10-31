// Real config writer/reader + diagnostic thread/reload, without game or XR.
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

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
std::atomic<bool> fake_session_mode{false};
std::atomic<bool> fake_stop_request{false};
std::atomic<bool> force_segment_rotation{false};
std::atomic<bool> force_segment_drop{false};
std::atomic<bool> fake_close_dropped{false};
std::atomic<unsigned long> fake_dropped_count{0};
std::atomic<unsigned> session_begin_count{0};
std::atomic<unsigned> session_epoch_two_begins{0};
std::atomic<unsigned> session_duration{0};
std::atomic<unsigned long long> fake_capture_id{0};
std::atomic<dsvr::TraceCaptureState> fake_trace_state{dsvr::TraceCaptureState::Idle};
std::wstring session_paths[8];
char session_ids[8][64]{};
unsigned long long session_epochs[8]{}, session_segments[8]{};
std::ofstream fake_trace_file;
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
double XrLastDisplayRefreshHz() { return 0.0; }  // sidecar write is a no-op keeper here
void LabPump() { ++lab_calls; }
void LabRequestAbort() { ++lab_calls; }
bool CameraHookInstall(bool vr, bool observer) { ++camera_calls; return !vr && observer && !refuse_camera; }
bool VrTraceInstall(bool enabled) { ++trace_calls; return enabled; }
bool VrTraceInstallCombat() { ++trace_calls;combat_install=true;return true; }
bool VrTraceBegin(const wchar_t*, unsigned duration) { capture_duration=duration; ++captures; return true; }
bool VrTraceInstallSession() { ++trace_calls; fake_session_mode=true; return true; }
bool VrTraceSessionHooksReady() { return fake_session_mode; }
unsigned VrTraceSessionCapabilities() {
  return kSessionCapNativeCombat | kSessionCapInteraction |
         kSessionCapVirtualPad | kSessionCapTrackedGrip;
}
bool InputAvailable() { return fake_session_mode; }
long long VrTraceClock() {
  LARGE_INTEGER value{};QueryPerformanceCounter(&value);return value.QuadPart;
}
bool VrTraceBeginSession(const wchar_t* path, unsigned duration, const char* session_id,
                         std::uint64_t epoch, std::uint64_t segment,
                         unsigned capabilities, unsigned config_flags) {
  const auto slot = session_begin_count.load();
  if (slot >= 8) return false;
  session_paths[slot] = path;
  std::snprintf(session_ids[slot], sizeof(session_ids[slot]), "%s", session_id);
  session_epochs[slot] = epoch;
  session_segments[slot] = segment;
  session_duration = duration;
  fake_dropped_count = 0;
  fake_trace_file.open(std::filesystem::path(path), std::ios::binary | std::ios::out);
  if (!fake_trace_file.good()) return false;
  fake_trace_file << "{\"event\":\"header\",\"session_id\":\"" << session_id
      << "\",\"player_epoch\":" << epoch << ",\"segment_index\":"
      << segment << ",\"session_capabilities\":" << capabilities
      << ",\"session_config_flags\":" << config_flags << "}\n";
  fake_trace_file.flush();
  fake_capture_id.fetch_add(1);
  fake_trace_state = TraceCaptureState::Recording;
  if (epoch == 2) ++session_epoch_two_begins;
  session_begin_count.fetch_add(1, std::memory_order_release);
  return true;
}
void VrTraceRequestStop() {
  if (fake_session_mode && fake_trace_state == TraceCaptureState::Recording)
    fake_trace_state = TraceCaptureState::Closing;
}
void VrTracePump() {
  if (!fake_session_mode) return;
  if (fake_trace_state == TraceCaptureState::Recording && force_segment_drop.exchange(false)) {
    fake_close_dropped = true;
    fake_trace_state = TraceCaptureState::Closing;
  }
  if (fake_trace_state == TraceCaptureState::Recording && force_segment_rotation.exchange(false))
    fake_trace_state = TraceCaptureState::Closing;
  if (fake_trace_state != TraceCaptureState::Closing) return;
  const bool dropped = fake_close_dropped.exchange(false);
  fake_dropped_count = dropped ? 1 : 0;
  fake_trace_file << "{\"event\":\"footer\",\"interrupted\":false,\"dropped\":"
      << (dropped ? 1 : 0) << "}\n";
  fake_trace_file.close();
  fake_trace_state = dropped ? TraceCaptureState::Dropped : TraceCaptureState::Complete;
}
void VrTraceFinish(bool interrupted) {
  if (!fake_session_mode || !fake_trace_file.is_open()) return;
  fake_trace_file << "{\"event\":\"footer\",\"interrupted\":"
      << (interrupted ? "true" : "false") << ",\"dropped\":0}\n";
  fake_trace_file.close();
  fake_trace_state = interrupted ? TraceCaptureState::Interrupted : TraceCaptureState::Complete;
}
bool VrTraceConsumeStopRequest(const wchar_t*) {
  if (!fake_stop_request.exchange(false)) return false;
  VrTraceRequestStop();
  return true;
}
void VrTraceSetExternalWriter(const wchar_t*) {}
bool VrTraceCheckpoint() { return true; }
TraceCaptureState VrTraceState() {
  return fake_session_mode ? fake_trace_state.load() : TraceCaptureState::Complete;
}
std::uint64_t VrTraceCaptureId() { return fake_capture_id.load(); }
VrTraceHealth VrTraceGetHealth() {
  VrTraceHealth h{};h.state=VrTraceState();h.capture_id=VrTraceCaptureId();
  h.dropped=fake_dropped_count.load();
  h.event_counts={{"footer",1},{"header",1}};
  return h;
}
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
  const bool session=std::string(argv[1])=="session";
  const bool session_stop=std::string(argv[1])=="session-stop";
  const bool observer = combat || std::string(argv[1]).starts_with("observer");
  const bool disabled = std::string(argv[1]) == "disabled";
  refuse_camera = std::string(argv[1]) == "observer-refused";
  CHECK(!DiagnosticsBeginCapture());
  wchar_t temp[MAX_PATH]{}, path[MAX_PATH]{};
  CHECK(GetTempPathW(MAX_PATH, temp) && GetTempFileNameW(temp, L"DSV", 0, path));
  Config requested;
  requested.observer_only = observer;
  requested.combat_trace_only = combat;
  // Observer mode is desktop-only: ConfigValid refuses the VR mix, so the
  // fixture must not build one either.
  requested.vr_enabled = (!disabled || session || session_stop) && !observer;
  requested.autonomous_session_capture = session || session_stop;
  requested.headlook_enabled = requested.xr_input_enabled = true;
  requested.hide_player_model = requested.hide_second_model = true;
  requested.motion_attacks = requested.haptics = requested.arm_ik_enabled = true;
  requested.arm_ik_log_only = false;
  requested.sanctuary_override = true;
  requested.hotkeys_enabled = false;  // never consume real desktop keys
  requested.verbose_logging = true;
  CHECK(ConfigSave(path, requested));
  if (session || session_stop) CHECK(VrTraceInstallSession());
  DiagnosticsStart(requested, path);
  if (session_stop) {
    Sleep(80);
    CHECK(session_begin_count == 0);
    player_loaded = true;
    for (unsigned i=0;i<200 && session_begin_count.load(std::memory_order_acquire)<1;++i) Sleep(10);
    CHECK(session_begin_count == 1);
    fake_stop_request = true;
    for (unsigned i=0;i<200 && fake_trace_state != TraceCaptureState::Complete;++i) Sleep(10);
    CHECK(fake_trace_state == TraceCaptureState::Complete);
    DiagnosticsStop();
    const std::string stopped_session_id(session_ids[0]);
    const std::wstring index_path = std::wstring(path) + L".session-" +
        std::wstring(stopped_session_id.begin(), stopped_session_id.end()) + L".jsonl";
    std::ifstream index(index_path, std::ios::binary);
    std::string index_text{std::istreambuf_iterator<char>(index),{}};
    CHECK(index_text.find("\"event\":\"capture_stopped\"") != std::string::npos);
    CHECK(index_text.find("\"reason\":\"operator_stop\"") != std::string::npos);
    index.close();
    CHECK(DeleteFileW(path));
    CHECK(DeleteFileW(index_path.c_str()));
    CHECK(DeleteFileW(session_paths[0].c_str()));
    std::puts("test_diagnostics_mode (session-stop): PASS");
    return 0;
  }
  if (session) {
    Sleep(80);
    CHECK(session_begin_count == 0);
    CHECK(!DiagnosticsBeginCapture()); // F9/menu must not compete with the session owner.
    player_loaded = true;
    for (unsigned i=0;i<200 && session_begin_count.load(std::memory_order_acquire)<1;++i) Sleep(10);
    CHECK(session_begin_count == 1 && session_duration == 600000);
    CHECK(session_epochs[0] == 1 && session_segments[0] == 1);
    const std::string session_id = session_ids[0];
    force_segment_rotation = true;
    for (unsigned i=0;i<200 && session_begin_count.load(std::memory_order_acquire)<2;++i) Sleep(10);
    CHECK(session_begin_count == 2 && session_ids[1] == session_id);
    CHECK(session_epochs[1] == 1 && session_segments[1] == 2);
    player_loaded = false;
    Sleep(1000); // a short unload must not split an epoch
    player_loaded = true;
    Sleep(100);
    CHECK(session_begin_count == 2 && session_epoch_two_begins == 0);
    force_segment_drop = true;
    for (unsigned i=0;i<200 && fake_trace_state != TraceCaptureState::Dropped;++i) Sleep(10);
    CHECK(fake_trace_state == TraceCaptureState::Dropped && session_begin_count == 2);
    player_loaded = false;
    Sleep(2250); // player absence must be continuous for at least two seconds
    CHECK(session_begin_count == 2);
    player_loaded = true;
    for (unsigned i=0;i<200 && session_begin_count.load(std::memory_order_acquire)<3;++i) Sleep(10);
    CHECK(session_begin_count == 3 && session_epochs[2] == 2 && session_segments[2] == 3);
    DiagnosticsStop(); // orderly worker shutdown closes the last segment as interrupted
    CHECK(fake_trace_state == TraceCaptureState::Interrupted);
    std::ifstream first(session_paths[0], std::ios::binary);
    std::string first_trace{std::istreambuf_iterator<char>(first),{}};
    CHECK(first_trace.find("\"interrupted\":false") != std::string::npos);
    std::ifstream last(session_paths[2], std::ios::binary);
    std::string last_trace{std::istreambuf_iterator<char>(last),{}};
    CHECK(last_trace.find("\"interrupted\":true") != std::string::npos);
    const std::wstring index_path = std::wstring(path) + L".session-" +
        std::wstring(session_id.begin(), session_id.end()) + L".jsonl";
    std::ifstream index(index_path, std::ios::binary);
    std::string index_text{std::istreambuf_iterator<char>(index),{}};
    auto has_utc_bound = [&](const char* event) {
      const auto marker = std::string("\"event\":\"") + event + "\"";
      const auto start = index_text.find(marker);
      if (start == std::string::npos) return false;
      const auto end = index_text.find(static_cast<char>(10), start);
      return index_text.substr(start, end - start).find("\"utc_filetime_100ns\":") !=
          std::string::npos;
    };
    CHECK(has_utc_bound("segment_start") && has_utc_bound("segment_end") &&
          has_utc_bound("epoch_start") && has_utc_bound("epoch_end") &&
          has_utc_bound("session_end"));
    CHECK(index_text.find("\"event\":\"segment_end\"") != std::string::npos);
    CHECK(index_text.find("\"event_counts\":{\"footer\":1,\"header\":1}") != std::string::npos);
    CHECK(index_text.find("\"reason\":\"player_unload_degraded\"") != std::string::npos);
    CHECK(index_text.find("\"reason\":\"dropped_events\"") != std::string::npos);
    CHECK(index_text.find("\"dropped\":1") != std::string::npos);
    CHECK(index_text.find("\"epoch\":2") != std::string::npos);
    const auto epoch_two_start = index_text.find("\"epoch\":2,\"segment\":3,");
    CHECK(epoch_two_start != std::string::npos);
    const auto epoch_two_end = index_text.find('\n', epoch_two_start);
    CHECK(index_text.substr(epoch_two_start, epoch_two_end - epoch_two_start)
              .find("\"gap_qpc\":0") != std::string::npos);
    CHECK(index_text.find("process_shutdown_interrupted") != std::string::npos);
    first.close();last.close();index.close();
    CHECK(DeleteFileW(path));
    CHECK(DeleteFileW(index_path.c_str()));
    for (unsigned i=0;i<session_begin_count;++i)
      CHECK(DeleteFileW(session_paths[i].c_str()));
    std::puts("test_diagnostics_mode (session): PASS");
    return 0;
  }
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
  // Keep the reloaded candidate valid: observer off + combat-only on is
  // refused, and observer on + VR on is refused.
  if (!requested.observer_only) requested.combat_trace_only = false;
  if (requested.observer_only) requested.vr_enabled = false;
  requested.autonomous_session_capture = true;
  requested.render_scale_percent = 125;
  CHECK(ConfigSave(path, requested));
  CHECK(DiagnosticsReloadConfig());
  c = DiagnosticsCurrentConfig();
  CHECK(c.observer_only == observer && c.render_scale_percent == 125);
  CHECK(c.combat_trace_only==combat);
  CHECK(!c.autonomous_session_capture);
  // The reloaded candidate flips observer on for the VR run, so the
  // startup-mode restore + RestrictObserver leave VR off: observer wins.
  CHECK(c.vr_enabled == (!observer && !disabled && !requested.observer_only) &&
        c.headlook_enabled == !observer);
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
