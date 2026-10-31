// DSVR loader: fresh dinput8.dll proxy.
//
// Jobs:
//   1. Forward DirectInput8Create -- to a chain-loaded proxy when configured
//      ([loader] chain_proxy in DSVR.toml), else to the real system DLL.
//      This keeps other dinput8 mods working alongside DSVR (QoL batch 1).
//   2. On process attach (worker thread, outside the loader lock), load
//      DSVR.dll from the same folder and let it run gate + VR core.
//   3. Log every step with flushes so a silent load is diagnosable.
//
// Everything else lives in DSVR.dll. This file knows no game offsets/hooks/VR.

#include <windows.h>

#include <objbase.h>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "fps_unlock_settings.h"

#include "../common/config.h"
#include "../common/logging.h"
#include "../common/version.h"

namespace {

HMODULE g_self = nullptr;
HMODULE g_forward_module = nullptr;
std::atomic<bool> g_forward_tried{false};

using DirectInput8CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*,
                                              LPUNKNOWN);
DirectInput8CreateFn g_real_create = nullptr;

std::wstring SystemDinput8Path() {
  wchar_t sysdir[32768]{};
  UINT n = GetSystemDirectoryW(sysdir, 32767);
  std::wstring p;
  if (n == 0 || n >= 32767) {
    p = L"C:\\Windows\\System32\\";
  } else {
    p = sysdir;
    if (!p.empty() && p.back() != L'\\') p += L'\\';
  }
  p += L"dinput8.dll";
  return p;
}

std::wstring ModulePath(HMODULE mod) {
  wchar_t path[32768]{};
  DWORD n = GetModuleFileNameW(mod, path, 32767);
  if (n == 0 || n >= 32767) return {};
  return std::wstring(path, n);
}

std::wstring DirOf(const std::wstring& path) {
  auto pos = path.find_last_of(L"\\/");
  return pos == std::wstring::npos ? L"." : path.substr(0, pos);
}

bool IsAbsolutePath(const std::wstring& p) {
  return p.size() >= 2 && p[1] == L':' ||
         (p.size() >= 2 && p[0] == L'\\' && p[1] == L'\\');
}

std::wstring ToLowerW(std::wstring s) {
  for (auto& c : s) c = (wchar_t)towlower(c);
  return s;
}

// Resolve forwarding target once. Chain proxy first (when configured and
// valid), system DLL as fallback. Safe to call from the export.
int ChooseDependencyMaxFps(const dsvr::Config& cfg, const std::wstring& config_path,
                           int& headset_hz, int& desktop_hz) {
  headset_hz = 0;
  desktop_hz = 0;
  if (cfg.vr_enabled && !cfg.observer_only) {
    headset_hz = dsvr::ReadLastHeadsetHz((config_path + L".headset-hz").c_str());
  } else {
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) &&
        mode.dmDisplayFrequency >= 10 && mode.dmDisplayFrequency <= 1000)
      desktop_hz = static_cast<int>(mode.dmDisplayFrequency);
  }
  return dsvr::fps_unlock::ChooseMaxFps(cfg.vr_enabled, cfg.observer_only,
                                         headset_hz, desktop_hz);
}

bool SyncFpsUnlockSettings(const std::wstring& chain_path,
                           const dsvr::Config& cfg,
                           const std::wstring& config_path) {
  std::wstring ini_path = DirOf(chain_path) + L"\\DSR-FPS-Unlock.ini";
  if (GetFileAttributesW(ini_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
    DSVR_WARN("Loader: chained proxy has no DSR-FPS-Unlock.ini; no FPS Unlock dependency settings applied (%ls)",
              ini_path.c_str());
    return false;
  }
  int headset_hz = 0;
  int desktop_hz = 0;
  const int max_fps = ChooseDependencyMaxFps(cfg, config_path, headset_hz, desktop_hz);
  std::ifstream input(std::filesystem::path(ini_path), std::ios::binary);
  if (!input) {
    DSVR_WARN("Loader: could not read dependent FPS Unlock INI (%ls)", ini_path.c_str());
    return false;
  }
  const std::string original{std::istreambuf_iterator<char>(input), {}};
  std::string updated = original;
  if (!dsvr::fps_unlock::SetFpsUnlockSettings(updated, max_fps)) {
    DSVR_WARN("Loader: refused invalid automatic refresh rate %d", max_fps);
    return false;
  }
  if (updated == original) {
    if (max_fps == 0)
      DSVR_LOG("Loader: no valid automatic refresh; FPS Unlock remains disabled; no preset cap used");
    else
      DSVR_LOG("Loader: DSR-FPS-Unlock MaxFPS already %d; VariableFrameTime enabled", max_fps);
    return true;
  }
  const std::wstring temp_path = ini_path + L".dsvr-" +
      std::to_wstring(GetCurrentProcessId()) + L".tmp";
  {
    std::ofstream output(std::filesystem::path(temp_path),
                         std::ios::binary | std::ios::trunc);
    if (!output) {
      DSVR_WARN("Loader: could not create dependent FPS Unlock settings temp file");
      return false;
    }
    output.write(updated.data(), static_cast<std::streamsize>(updated.size()));
    output.close();
    if (!output) {
      DeleteFileW(temp_path.c_str());
      DSVR_WARN("Loader: could not write dependent FPS Unlock settings");
      return false;
    }
  }
  if (!MoveFileExW(temp_path.c_str(), ini_path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(temp_path.c_str());
    DSVR_WARN("Loader: could not atomically update dependent FPS Unlock settings (%lu)",
              GetLastError());
    return false;
  }
  if (max_fps == 0) {
    DSVR_WARN("Loader: no valid headset/monitor refresh; disabled DSR-FPS-Unlock instead of using a preset");
    return true;
  }
  const char* source = headset_hz ? "last-known headset refresh" : "current desktop refresh";
  DSVR_LOG("Loader: DSR-FPS-Unlock MaxFPS=%d from %s; FPSUnlock and VariableFrameTime enabled; "
           "refresh changes apply next launch because dependency reads its INI once",
           max_fps, source);
  return true;
}

void EnsureForward() {
  if (g_real_create) return;
  bool expected = false;
  if (!g_forward_tried.compare_exchange_strong(expected, true)) {
    // Another thread is resolving; spin briefly for the function pointer.
    for (int i = 0; i < 200 && !g_real_create; i++) Sleep(1);
    return;
  }

  const std::wstring cfg_path = dsvr::ConfigPathNextToExe();
  dsvr::Config cfg;
  const bool config_loaded = dsvr::ConfigLoad(cfg_path.c_str(), cfg);
  std::wstring chain_path;
  if (config_loaded && !cfg.loader_chain_proxy.empty()) {
    std::wstring chain = dsvr::WideFromUtf8(cfg.loader_chain_proxy);
    if (IsAbsolutePath(chain)) chain_path = chain;
    else chain_path = dsvr::GameExeDirectory() + L"\\" + chain;
  }

  std::wstring self_path = ToLowerW(ModulePath(g_self));
  if (!chain_path.empty()) {
    if (ToLowerW(chain_path) == self_path) {
      DSVR_WARN("Loader: chain_proxy points at DSVR itself; ignoring (loop)");
    } else if (GetFileAttributesW(chain_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
      DSVR_WARN("Loader: chain_proxy not found (%ls); using system dinput8",
                chain_path.c_str());
    } else {
      // DSR-FPS-Unlock reads its INI in its DllMain worker, so synchronize its
      // MaxFPS before LoadLibrary starts that worker.
      if (config_loaded) SyncFpsUnlockSettings(chain_path, cfg, cfg_path);
      HMODULE chain_mod = LoadLibraryW(chain_path.c_str());
      if (!chain_mod) {
        DSVR_WARN("Loader: chain load failed (%lu) %ls; using system",
                  GetLastError(), chain_path.c_str());
      } else {
        auto fn = (DirectInput8CreateFn)GetProcAddress(
            chain_mod, "DirectInput8Create");
        if (!fn) {
          DSVR_WARN("Loader: chain has no DirectInput8Create %ls; using system",
                    chain_path.c_str());
        } else {
          g_forward_module = chain_mod;
          g_real_create = fn;
          DSVR_LOG("Loader: forwarding DirectInput8Create to chain %ls",
                   chain_path.c_str());
          return;
        }
      }
    }
  } else {
    DSVR_WARN("Loader: no FPS Unlock dependency chained; DSVR native rate setter is disabled, "
              "so high-refresh support is unavailable until DSR-FPS-Unlock is installed");
  }

  std::wstring sys = SystemDinput8Path();
  HMODULE sysmod = LoadLibraryW(sys.c_str());
  if (!sysmod) {
    DSVR_ERROR("Loader: failed to load system dinput8 (%lu)", GetLastError());
    return;
  }
  auto fn = (DirectInput8CreateFn)GetProcAddress(sysmod, "DirectInput8Create");
  if (!fn) {
    DSVR_ERROR("Loader: system dinput8 has no DirectInput8Create");
    return;
  }
  g_forward_module = sysmod;
  g_real_create = fn;
  DSVR_LOG("Loader: forwarding DirectInput8Create to system dinput8");
}

DWORD WINAPI LoaderThread(LPVOID /*param*/) {
  std::wstring log_path = dsvr::LogPathNextToModule(nullptr, L"DSVR.log");
  dsvr::LogInit(log_path.c_str());
  DSVR_LOG("Loader: DSVR %s starting", dsvr::kModVersion);
  DSVR_LOG("Loader: WARNING: offline play only while DSVR is installed. "
           "Remove dinput8.dll + DSVR.dll before any online play.");

  EnsureForward();

  std::wstring core = dsvr::GameExeDirectory() + L"\\" +
                      dsvr::WideFromUtf8(dsvr::kCoreDllName);
  DSVR_LOG("Loader: loading core %ls", core.c_str());
  HMODULE core_mod = LoadLibraryW(core.c_str());
  if (!core_mod) {
    DSVR_ERROR("Loader: core load failed (%lu); game continues vanilla",
               GetLastError());
    return 1;
  }
  using CoreStartFn = unsigned long(__stdcall*)();
  auto start = (CoreStartFn)GetProcAddress(core_mod, "DsvrCoreStart");
  if (!start) {
    DSVR_ERROR("Loader: core has no DsvrCoreStart export");
    return 1;
  }
  unsigned long rc = start();
  DSVR_LOG("Loader: core start returned %lu (0 gate-passed, else refused)",
           rc);
  return 0;
}

}  // namespace

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE inst, DWORD ver,
                                             REFIID iid, LPVOID* out,
                                             LPUNKNOWN unk) {
  EnsureForward();
  if (!g_real_create) return E_FAIL;
  return g_real_create(inst, ver, iid, out, unk);
}

BOOL APIENTRY DllMain(HMODULE self, DWORD reason, LPVOID /*reserved*/) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_self = self;
    DisableThreadLibraryCalls(self);
    HANDLE t = CreateThread(nullptr, 0, LoaderThread, nullptr, 0, nullptr);
    if (t) CloseHandle(t);
  }
  return TRUE;
}
