#include "diagnostics.h"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <mutex>
#include <optional>
#include <deque>

#include "../common/logging.h"
#include "../common/memory.h"
#include "xr.h"
#include "camhook.h"
#include "vr_trace.h"
#include "lab_runner.h"
#include "mod_menu.h"
#include "calibration.h"
#include "combat_native.h"

namespace dsvr {
namespace {

std::mutex g_mutex;
Config g_config, g_saved_config;
bool g_settings_busy = false;
bool g_initialized = false;  // process-lifetime mode, not diagnostic-thread lifetime
bool g_observer_hooks_attempted = false;
bool g_observer_hooks_ready = false;
std::wstring g_config_path;
std::atomic<bool> g_running{false};
HANDLE g_thread = nullptr;
unsigned long g_marker_count = 0;
std::optional<Config> g_pending_config;
bool g_pending_save = false, g_export_logs = false;
std::string g_settings_status;
std::deque<std::pair<std::string,std::function<bool()>>> g_commands;

void PumpMenuRequests() {
  std::optional<Config> pending;
  bool save = false, export_logs = false;
  std::wstring path;
  std::pair<std::string,std::function<bool()>> command;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    pending.swap(g_pending_config);
    save = g_pending_save;
    export_logs = g_export_logs;
    g_export_logs = false;
    path = g_config_path;
    if (!g_commands.empty()) {command=std::move(g_commands.front());g_commands.pop_front();}
  }
  if (pending) {
    bool ok = true;
    if (save) {
      const auto temporary = path + L".tmp";
      ok = ConfigSave(temporary.c_str(), *pending) &&
          MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
      if (!ok) DeleteFileW(temporary.c_str());
    }
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (ok) {
        g_config = *pending;
        if (save) g_saved_config = *pending;
      }
      g_settings_busy = false;
      g_settings_status = ok ? (save ? "Settings saved." : "Settings applied for this session.") :
                               "Could not save settings. Previous settings remain active.";
    }
    if (!ok) DSVR_WARN("Menu: could not save settings");
  }
  if (export_logs) {
    const auto output = path + L".support.log";
    FILE* file = nullptr;
    bool ok = _wfopen_s(&file, output.c_str(), L"wb") == 0 && file;
    if (ok) {
      for (const auto& line : LogRecent()) fputs(line.text.c_str(), file);
      const bool written = ferror(file) == 0;
      ok = fclose(file) == 0 && written;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_settings_status = ok ? "Recent logs exported beside DSVR.toml as DSVR.toml.support.log." :
                             "Could not export logs. Check game-folder write permissions.";
  }
  if (command.second) {
    const bool ok=command.second();
    std::lock_guard<std::mutex> lock(g_mutex);
    g_settings_status=command.first+(ok?" complete.":" failed. Previous settings were preserved.");
  }
}

bool IsFiniteF(float v) { return std::isfinite(v) && v > -100000.0f && v < 100000.0f; }

bool GameIsForeground() {
  HWND fg = GetForegroundWindow();
  if (!fg) return false;
  DWORD pid = 0;
  GetWindowThreadProcessId(fg, &pid);
  return pid == GetCurrentProcessId();
}

void LogSnapshot() {
  std::uintptr_t base = ModuleBase(nullptr);
  DSVR_LOG("Diagnostics: snapshot base=%p", (void*)base);

  // WorldChrManImp* at exe+0x1c77e50; PlayerIns = [WorldChrMan+0x68].
  // Every step is guarded: title screen / loading simply logs "not loaded".
  std::uintptr_t world = 0;
  std::uintptr_t offsets_world[]{0x68};
  bool in_world = false;
  float px = 0, py = 0, pz = 0;
  if (GuardedRead(base + 0x1c77e50, world) && world) {
    auto player = FollowChain(world, offsets_world, 1);
    if (player && *player) {
      // Physics pos: PlayerIns+0x7b8 -> +0x2e0 -> +0x120 (float x,y,z,w).
      std::uintptr_t hops[]{0x7b8, 0x2e0};
      auto mid = FollowChain(*player, hops, 2);
      if (mid) {
        float vec[4]{};
        if (GuardedReadBytes(*mid + 0x120, vec, sizeof(vec)) &&
            IsFiniteF(vec[0]) && IsFiniteF(vec[1]) && IsFiniteF(vec[2])) {
          px = vec[0];
          py = vec[1];
          pz = vec[2];
          in_world = true;
        }
      }
    }
  }
  if (in_world) {
    DSVR_LOG("Diagnostics: player physics pos = %.3f, %.3f, %.3f", px, py, pz);
  } else {
    DSVR_LOG("Diagnostics: player not loaded (title/loading) -- expected there");
  }

  // Skeleton RE probe (read-only, guarded, no writes). One F9 per pose
  // gives the live session everything the offline analyzer needs:
  // PlayerIns + both render-model pointers + body yaw. Bone contents are
  // never touched here; the running-image dump (separate approved step)
  // resolves the bone-array layout itself.
  {
    std::uintptr_t world2 = 0;
    std::uintptr_t off_w[]{0x68};
    auto pl = GuardedRead(base + 0x1c77e50, world2) && world2
                  ? FollowChain(world2, off_w, 1)
                  : std::nullopt;
    if (pl && *pl) {
      std::uintptr_t m60 = 0, m850 = 0, ctrl = 0, posdata = 0;
      float yaw = 0.0f;
      const bool ok60 = GuardedRead(*pl + 0x60, m60);
      const bool ok850 = GuardedRead(*pl + 0x850, m850);
      const bool okyaw = GuardedRead(*pl + 0x18, ctrl) && ctrl &&
                         GuardedRead(ctrl + 0x28, posdata) && posdata &&
                         GuardedRead(posdata + 0x4, yaw) &&
                         std::isfinite(yaw);
      if (ok60 && ok850) {
        if (okyaw) {
          DSVR_LOG("Skeleton: player=%p model60=%p model850=%p yaw=%.3f",
                   (void*)*pl, (void*)m60, (void*)m850, (double)yaw);
        } else {
          DSVR_LOG("Skeleton: player=%p model60=%p model850=%p yaw=unavailable",
                   (void*)*pl, (void*)m60, (void*)m850);
        }
      } else {
        DSVR_LOG("Skeleton: model pointers unavailable (title/loading)");
      }
    }
  }

  // The global is a 0x18-byte container, not a matrix owner. Leave camera
  // geometry to the natural-owner observer; no off-thread slot inference.
  std::uintptr_t camman = 0;
  if (GuardedRead(base + 0x1c6e188, camman) && camman) {
    DSVR_LOG("Diagnostics: camera container=%p (active slot unresolved)",
             reinterpret_cast<void*>(camman));
  }

  // Read-only menu lifecycle probe. Running-image decompilation shows the
  // shared FrpgMenuDialog update consulting these four values through the
  // central action-state block. We deliberately log raw values rather than
  // guessing a policy: one marked sample each in play/inventory/pause/title
  // can establish which transition is authoritative for every menu family.
  std::uintptr_t action_states = 0;
  int state_11 = 0, state_39 = 0;
  unsigned char menu_a = 0, menu_b = 0, gate = 0;
  const bool menu_probe_ok =
      GuardedRead(base + 0x1c88d98, action_states) && action_states &&
      GuardedRead(action_states + 0x30 + 0x11 * sizeof(int), state_11) &&
      GuardedRead(action_states + 0x30 + 0x39 * sizeof(int), state_39) &&
      GuardedRead(action_states + 0xa40, menu_a) &&
      GuardedRead(action_states + 0xa41, menu_b) &&
      GuardedRead(base + 0x1c847b0, gate);
  if (menu_probe_ok) {
    DSVR_LOG("Diagnostics: menu probe state11=%d state39=%d a40=%u a41=%u "
             "gate=%u",
             state_11, state_39, (unsigned)menu_a, (unsigned)menu_b,
             (unsigned)gate);
  } else {
    DSVR_LOG("Diagnostics: menu probe unavailable (state block not loaded)");
  }
}

bool ReloadConfig() {
  Config fresh;
  std::wstring path;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    path = g_config_path;
  }
  if (!ConfigLoad(path.c_str(), fresh)) {
    DSVR_WARN("Diagnostics: reload failed (cannot read %ls); keeping live config",
              path.c_str());
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (fresh.observer_only != g_config.observer_only)
      DSVR_WARN("Diagnostics: observer_only change ignored until process restart");
    fresh.observer_only = g_config.observer_only;
    if(fresh.combat_trace_only!=g_config.combat_trace_only)
      DSVR_WARN("Diagnostics: combat_trace_only change ignored until process restart");
    fresh.combat_trace_only=g_config.combat_trace_only;
    ConfigRestrictObserver(fresh);
    if (g_settings_busy) return false;
    g_config = fresh;
    g_saved_config = fresh;
  }
  DSVR_LOG("Diagnostics: config reloaded vr=%d scale=%d motion=%d haptics=%d "
           "hand=%s style=%s",
           (int)fresh.vr_enabled, fresh.render_scale_percent,
           (int)fresh.motion_attacks, (int)fresh.haptics,
           fresh.dominant_hand_left ? "left" : "right",
           fresh.play_style.c_str());
  DSVR_LOG("Diagnostics: NOTE rendering-host selection requires restart; "
           "render-scale/combat changes fully apply next "
            "restart until P2+ hooks read live config each frame");
  return true;
}

DWORD WINAPI DiagnosticsThread(LPVOID /*param*/) {
  bool marker_down = false;
  bool reload_down = false;
  bool sanctuary_down = false;
  bool recenter_down = false;
  bool home_down = false;
  unsigned long long next_combat_health=0;
  unsigned previous_combat_health=~0u;
  DSVR_LOG("Diagnostics: hotkeys ready (marker=F-key 0x%02x, reload=0x%02x)",
           DiagnosticsCurrentConfig().log_marker_vk,
           DiagnosticsCurrentConfig().config_reload_vk);
  while (g_running.load(std::memory_order_acquire)) {
    Config cfg = DiagnosticsCurrentConfig();
    const auto now=GetTickCount64();
    if(now>=next_combat_health) {
      const auto combat=CombatNativeGetStatus();
      const unsigned health=unsigned(combat.equipment_bound) |
          (unsigned(combat.physical_damage_bound)<<1) |
          (unsigned(combat.shield_contact_bound)<<2) |
          (unsigned(combat.two_hand_rules_bound)<<3);
      if(health!=previous_combat_health || cfg.verbose_logging) {
        DSVR_LOG("CombatHealth: equipment=%d damage_binding=%d shield_binding=%d "
                 "two_hand_binding=%d motion_requested=%d observer_only=%d "
                 "weapons=%d/%d stance=%d; bench classifications are NOT native outcomes",
                 combat.equipment_bound,combat.physical_damage_bound,
                 combat.shield_contact_bound,combat.two_hand_rules_bound,
                 cfg.motion_attacks,cfg.observer_only,combat.equipment.weapon_id[0],
                 combat.equipment.weapon_id[1],combat.equipment.native_stance);
        previous_combat_health=health;
      }
      next_combat_health=now+5000;
    }
    if (cfg.hotkeys_enabled && GameIsForeground()) {
      bool m = cfg.log_marker_vk > 0 &&
               (GetAsyncKeyState(cfg.log_marker_vk) & 0x8000) != 0;
      bool r = cfg.config_reload_vk > 0 &&
               (GetAsyncKeyState(cfg.config_reload_vk) & 0x8000) != 0;
      if (m && !marker_down) DiagnosticsLogMarker();
      if (r && !reload_down) ReloadConfig();
      bool s = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
      bool c = (GetAsyncKeyState(VK_F6) & 0x8000) != 0;
      bool home = (GetAsyncKeyState(VK_HOME) & 0x8000) != 0;
      if (!cfg.observer_only && home && !home_down) ModMenuToggle();
      if (!cfg.observer_only && s && !sanctuary_down) XrToggleSanctuary();
      if (!cfg.observer_only && c && !recenter_down) {
        XrRecenterSanctuary();
        CalibrationRecenter();
      }
      home_down = home;
      sanctuary_down = s;
      recenter_down = c;
      marker_down = m;
      reload_down = r;
    } else {
      marker_down = false;
      reload_down = false;
      sanctuary_down = recenter_down = false;
      home_down = false;
    }
    if (!cfg.observer_only) LabPump();
    VrTracePump();
    PumpMenuRequests();
    Sleep(40);
  }
  // This path runs on the writer, not an animation/render callback or DllMain.
  // Hard process termination cannot be acknowledged: its trace stays incomplete.
  if (!DiagnosticsCurrentConfig().observer_only) {
    LabRequestAbort();
    LabPump();
  }
  VrTraceFinish(true);
  const auto until = GetTickCount64() + 1000;
  while (VrTraceState() == TraceCaptureState::Closing && GetTickCount64() < until) {
    VrTracePump();
    Sleep(1);
  }
  return 0;
}

}  // namespace

void DiagnosticsStart(const Config& initial, std::wstring config_path) {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_initialized) return;
    g_initialized = true;
    g_config = initial;
    ConfigRestrictObserver(g_config);
    g_saved_config = g_config;
    g_config_path = std::move(config_path);
  }
  if (g_running.exchange(true)) return;
  g_thread = CreateThread(nullptr, 0, DiagnosticsThread, nullptr, 0, nullptr);
  if (!g_thread) {
    g_running.store(false);
    DSVR_WARN("Diagnostics: hotkey thread failed (%lu)", GetLastError());
  }
}

void DiagnosticsStop() {
  g_running.store(false);
  if (g_thread) {
    WaitForSingleObject(g_thread, 1500);
    CloseHandle(g_thread);
    g_thread = nullptr;
  }
}

Config DiagnosticsCurrentConfig() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_config;
}

SettingsSnapshot DiagnosticsSettingsSnapshot() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return {g_config, g_saved_config, g_settings_busy};
}

bool DiagnosticsReloadConfig() { return ReloadConfig(); }

bool DiagnosticsRequestConfig(const Config& requested, bool save) {
  if (!ConfigValid(requested)) return false;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_initialized || !g_running.load() || g_settings_busy) return false;
  Config effective = requested;
    effective.observer_only = g_config.observer_only;
    effective.combat_trace_only = g_config.combat_trace_only;
  ConfigRestrictObserver(effective);
  g_pending_config = effective;
  g_pending_save = save;
  g_settings_busy = true;
  g_settings_status = "Applying settings...";
  return true;
}

void DiagnosticsRequestLogExport() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_export_logs = true;
  g_settings_status = "Exporting recent logs...";
}

std::string DiagnosticsSettingsStatus() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_settings_status;
}

bool DiagnosticsRequestCommand(const char* label,std::function<bool()> command) {
  if (!label || !command) return false;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_initialized || g_config.observer_only || g_commands.size()>=16) return false;
  g_commands.emplace_back(label,std::move(command));
  return true;
}

void DiagnosticsLogMarker() {
  unsigned long n = ++g_marker_count;
  SYSTEMTIME st{};
  GetLocalTime(&st);
  DSVR_LOG("=== MARKER #%lu %04u-%02u-%02u %02u:%02u:%02u (describe what "
           "you saw, then send DSVR.log) ===",
           n, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
  LogSnapshot();
  DiagnosticsBeginCapture();
}

bool DiagnosticsBeginCapture() {
  Config config;
  bool install = false;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_initialized) return false;
    config = g_config;
    if (config.observer_only) {
      if (g_observer_hooks_attempted && !g_observer_hooks_ready) return false;
      install = !g_observer_hooks_attempted;
      g_observer_hooks_attempted = true;
    }
  }
  if (install) {
    // No mutex across native installation. Only observational rights, unchanged
    // across reload/restart of diagnostics; never acquire XR/body/pad permissions.
    const bool camera = config.combat_trace_only || CameraHookInstall(false, true);
    const bool trace = config.combat_trace_only ? VrTraceInstallCombat() :
                       camera && VrTraceInstall(true);
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      g_observer_hooks_ready = camera && trace;
    }
    DSVR_LOG("Diagnostics: deferred observer hooks camera=%d trace=%d", camera, trace);
    if (!camera || !trace) return false;
  }
  return (config.observer_only || config.verbose_logging) && VrTraceBegin();
}

}  // namespace dsvr
