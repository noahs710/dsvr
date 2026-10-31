// DSVR-Setup: fresh installer/configurator (C++ Win32, zero extra runtime).
//
// Modes:
//   (no args) / --gui          friendly configurator window (see gui.cpp)
//   --verify | --doctor        read-only health checks
//   --install | --uninstall    CLI install (scripts, support, automation)
//   --firewall-block/-unblock/-status   offline-safety firewall rules
//
// Safety rules enforced here: version PIN before install, refuse while the
// game runs, always back up saves first, never delete a foreign dinput8.dll
// (offer chain-load instead), verify copied DLL hashes, offline ack required.

#include <windows.h>

#include <TlHelp32.h>
#include <shellapi.h>

#include <cstdio>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "../common/blur_files.h"
#include "../common/blur_state.h"
#include "../common/config.h"
#include "../common/memory.h"
#include "../common/version.h"
#include "gui.h"
#include "setup_internal.h"

namespace setup {
namespace {

std::wstring SystemFile(const wchar_t* name) {
  wchar_t sysdir[32768]{};
  UINT n = GetSystemDirectoryW(sysdir, 32767);
  std::wstring p = (n > 0 && n < 32767) ? std::wstring(sysdir, n)
                                        : L"C:\\Windows\\System32";
  return JoinPath(p, name);
}

bool IsDirectory(const std::wstring& p) {
  DWORD a = GetFileAttributesW(p.c_str());
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

std::vector<std::wstring> SteamLibraryPaths() {
  std::vector<std::wstring> libs;
  libs.push_back(L"C:\\Program Files (x86)\\Steam");
  wchar_t pf86[32768]{};
  if (GetEnvironmentVariableW(L"ProgramFiles(x86)", pf86, 32767) > 0) {
    std::wstring s = std::wstring(pf86) + L"\\Steam";
    if (s != libs[0]) libs.push_back(s);
  }
  std::wstring vdf = JoinPath(libs[0], L"steamapps\\libraryfolders.vdf");
  FILE* f = nullptr;
  if (_wfopen_s(&f, vdf.c_str(), L"r") != 0 || !f) return libs;
  char line[1024];
  while (fgets(line, sizeof(line), f)) {
    std::string v(line);
    auto pos = v.find("\"path\"");
    if (pos == std::string::npos) continue;
    auto q1 = v.find('"', pos + 6);
    if (q1 == std::string::npos) continue;
    auto q2 = v.find('"', q1 + 1);
    if (q2 == std::string::npos || q2 <= q1 + 1) continue;
    std::string raw = v.substr(q1 + 1, q2 - q1 - 1);
    std::string clean;
    for (size_t i = 0; i < raw.size(); i++) {
      if (raw[i] == '\\' && i + 1 < raw.size() && raw[i + 1] == '\\') {
        clean += '\\';
        i++;
      } else {
        clean += raw[i];
      }
    }
    wchar_t w[1024]{};
    MultiByteToWideChar(CP_UTF8, 0, clean.c_str(), -1, w, 1023);
    if (w[0]) libs.push_back(w);
  }
  fclose(f);
  return libs;
}

bool IsElevated() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    return false;
  TOKEN_ELEVATION el{};
  DWORD size = 0;
  bool ok = GetTokenInformation(token, TokenElevation, &el, sizeof(el),
                                &size) &&
            el.TokenIsElevated != 0;
  CloseHandle(token);
  return ok;
}

// Run a program hidden, capture combined output. Returns exit code (-1 fail).
int RunCapture(const std::wstring& exe, const std::wstring& args,
               std::wstring& output) {
  output.clear();
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  HANDLE read_pipe = nullptr, write_pipe = nullptr;
  if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) return -1;
  SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = write_pipe;
  si.hStdError = write_pipe;
  PROCESS_INFORMATION pi{};
  std::wstring cmd = L"\"" + exe + L"\" " + args;
  std::vector<wchar_t> mutable_cmd(cmd.begin(), cmd.end());
  mutable_cmd.push_back(L'\0');
  BOOL created = CreateProcessW(exe.c_str(), mutable_cmd.data(), nullptr,
                                nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                nullptr, &si, &pi);
  CloseHandle(write_pipe);
  if (!created) {
    CloseHandle(read_pipe);
    return -1;
  }
  char buf[4096];
  DWORD n = 0;
  std::string bytes;
  while (ReadFile(read_pipe, buf, sizeof(buf), &n, nullptr) && n > 0)
    bytes.append(buf, n);
  CloseHandle(read_pipe);
  WaitForSingleObject(pi.hProcess, 60000);
  DWORD code = 0;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  int wn = MultiByteToWideChar(CP_ACP, 0, bytes.data(), (int)bytes.size(),
                               nullptr, 0);
  if (wn > 0) {
    output.assign((size_t)wn, L'\0');
    MultiByteToWideChar(CP_ACP, 0, bytes.data(), (int)bytes.size(),
                        output.data(), wn);
  }
  return (int)code;
}

const wchar_t* kRuleOut = L"DSVR Offline Block (Out)";
const wchar_t* kRuleIn = L"DSVR Offline Block (In)";

bool FirewallRuleExists(const wchar_t* name) {
  std::wstring out;
  std::wstring args = L"advfirewall firewall show rule name=\"" +
                      std::wstring(name) + L"\"";
  int rc = RunCapture(SystemFile(L"netsh.exe"), args, out);
  if (rc < 0) return false;
  return out.find(L"No rules match") == std::wstring::npos &&
         out.find(name) != std::wstring::npos;
}

bool FirewallOne(const std::wstring& exe, const wchar_t* name,
                 const wchar_t* dir, bool add, const LogFn& log) {
  std::wstring args;
  if (add) {
    args = L"advfirewall firewall add rule name=\"" + std::wstring(name) +
           L"\" dir=" + dir + L" program=\"" + exe +
           L"\" action=block enable=yes profile=any";
  } else {
    args = L"advfirewall firewall delete rule name=\"" +
           std::wstring(name) + L"\"";
  }
  std::wstring out;
  int rc = RunCapture(SystemFile(L"netsh.exe"), args, out);
  if (rc != 0) {
    log(L"  Firewall " + std::wstring(add ? L"add" : L"remove") + L" '" +
        name + L"' failed (netsh exit " + std::to_wstring(rc) +
        L"). Run DSVR-Setup as Administrator, or add the rule by hand.");
    return false;
  }
  log(L"  Firewall rule '" + std::wstring(name) +
      (add ? L"' added (exe blocked)." : L"' removed."));
  return true;
}

void FindSavesUnder(const std::wstring& root, int depth,
                    std::vector<std::wstring>& found) {
  if (depth < 0 || found.size() >= 32) return;
  if (!IsDirectory(root)) return;
  WIN32_FIND_DATAW fd{};
  HANDLE h = FindFirstFileW(JoinPath(root, L"*").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  do {
    if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
    std::wstring child = JoinPath(root, fd.cFileName);
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      FindSavesUnder(child, depth - 1, found);
    } else if (!_wcsicmp(fd.cFileName, L"DRAKS0005.sl2")) {
      found.push_back(child);
    }
    if (found.size() >= 32) break;
  } while (FindNextFileW(h, &fd));
  FindClose(h);
}

std::vector<std::wstring> FindSaveFiles() {
  std::vector<std::wstring> found;
  wchar_t profile[32768]{}, appdata[32768]{};
  GetEnvironmentVariableW(L"USERPROFILE", profile, 32767);
  GetEnvironmentVariableW(L"APPDATA", appdata, 32767);
  FindSavesUnder(
      std::wstring(profile) + L"\\Documents\\NBGI\\DARK SOULS REMASTERED", 4,
      found);
  FindSavesUnder(std::wstring(appdata) + L"\\DarkSoulsRemastered", 4, found);
  for (const auto& lib : SteamLibraryPaths()) {
    FindSavesUnder(JoinPath(lib, L"userdata"), 5, found);
    if (found.size() >= 32) break;
  }
  return found;
}

bool CopyTree(const std::wstring& src, const std::wstring& dst) {
  if (!PathExists(src)) return true;  // absent is fine
  CreateDirectoryW(dst.c_str(), nullptr);
  WIN32_FIND_DATAW fd{};
  HANDLE h = FindFirstFileW(JoinPath(src, L"*").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return true;
  bool ok = true;
  do {
    if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
    std::wstring s = JoinPath(src, fd.cFileName);
    std::wstring d = JoinPath(dst, fd.cFileName);
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      if (!CopyTree(s, d)) ok = false;
    } else if (!CopyFileW(s.c_str(), d.c_str(), FALSE)) {
      ok = false;
    }
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  return ok;
}

std::wstring BackupSaves(const LogFn& log) {
  wchar_t profile[32768]{};
  GetEnvironmentVariableW(L"USERPROFILE", profile, 32767);
  std::wstring base = std::wstring(profile) + L"\\.dsvr-backups";
  CreateDirectoryW(base.c_str(), nullptr);
  SYSTEMTIME st{};
  GetLocalTime(&st);
  wchar_t sub[64]{};
  swprintf_s(sub, L"\\saves-%04u%02u%02u-%02u%02u%02u", st.wYear, st.wMonth,
             st.wDay, st.wHour, st.wMinute, st.wSecond);
  std::wstring dest = base + sub;
  CreateDirectoryW(dest.c_str(), nullptr);
  wchar_t appdata[32768]{};
  GetEnvironmentVariableW(L"APPDATA", appdata, 32767);
  CopyTree(std::wstring(profile) + L"\\Documents\\NBGI\\DARK SOULS REMASTERED",
           dest + L"\\NBGI-DARK-SOULS-REMASTERED");
  CopyTree(std::wstring(appdata) + L"\\DarkSoulsRemastered",
           dest + L"\\AppData-DarkSoulsRemastered");
  log(L"  Saves backed up to " + dest);
  log(L"  Keep this backup. Never take a modded save online.");
  return dest;
}

std::wstring SelfDir() {
  wchar_t p[32768]{};
  DWORD n = GetModuleFileNameW(nullptr, p, 32767);
  std::wstring s(p, (n < 32767) ? n : 0);
  return DirNameOf(s);
}

bool ShaOf(const std::wstring& path, std::string& out) {
  return dsvr::FileSha256(path.c_str(), out);
}

// A previous DSVR proxy is ours (safe to upgrade in place); any other
// dinput8.dll is a foreign mod (never deleted, chain-load instead).
// Ours is identified by the embedded "DSVR" log strings + small size; a
// hash-identical file is handled by the caller before this is needed.
bool LooksLikeOurProxy(const std::wstring& path) {
  HANDLE f = CreateFileW(path.c_str(), GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         OPEN_EXISTING, 0, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER sz{};
  GetFileSizeEx(f, &sz);
  bool small = sz.QuadPart > 0 && sz.QuadPart <= 512 * 1024;
  std::string bytes((size_t)sz.QuadPart, '\0');
  DWORD read = 0;
  bool ok = small &&
            ReadFile(f, bytes.data(), (DWORD)bytes.size(), &read, nullptr) &&
            read == (DWORD)bytes.size();
  CloseHandle(f);
  return ok && bytes.find("DsvrCoreStart") != std::string::npos;
}

}  // namespace

std::wstring JoinPath(const std::wstring& dir, const std::wstring& file) {
  if (dir.empty()) return file;
  if (dir.back() == L'\\' || dir.back() == L'/') return dir + file;
  return dir + L"\\" + file;
}

bool PathExists(const std::wstring& p) {
  return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::wstring DirNameOf(const std::wstring& path) {
  auto pos = path.find_last_of(L"\\/");
  return pos == std::wstring::npos ? L"." : path.substr(0, pos);
}

std::wstring FindGameExe(const std::wstring& override_path) {
  const wchar_t* rel =
      L"steamapps\\common\\DARK SOULS REMASTERED\\DarkSoulsRemastered.exe";
  if (!override_path.empty()) {
    std::wstring p = override_path;
    if (PathExists(JoinPath(p, L"DarkSoulsRemastered.exe")))
      return JoinPath(p, L"DarkSoulsRemastered.exe");
    if (PathExists(p)) return p;
    return L"";
  }
  for (const auto& lib : SteamLibraryPaths()) {
    std::wstring p = JoinPath(lib, rel);
    if (PathExists(p)) return p;
  }
  return L"";
}

bool IsGameRunning() {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return false;
  PROCESSENTRY32W pe{};
  pe.dwSize = sizeof(pe);
  bool running = false;
  if (Process32FirstW(snap, &pe)) {
    do {
      if (_wcsicmp(pe.szExeFile, L"DarkSoulsRemastered.exe") == 0) {
        running = true;
        break;
      }
    } while (Process32NextW(snap, &pe));
  }
  CloseHandle(snap);
  return running;
}

bool VerifyExe(const std::wstring& exe, const LogFn& log) {
  log(L"Game: " + exe);
  LARGE_INTEGER size{};
  HANDLE f = CreateFileW(exe.c_str(), GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         OPEN_EXISTING, 0, nullptr);
  if (f == INVALID_HANDLE_VALUE) {
    log(L"FAIL: cannot open exe. Close the game / check permissions.");
    return false;
  }
  GetFileSizeEx(f, &size);
  CloseHandle(f);
  std::wstring supported;
  for (unsigned i = 0; i < dsvr::kSupportedBuildCount; ++i) {
    if (i) supported += L"; ";
    supported += dsvr::WideFromUtf8(dsvr::kSupportedBuilds[i].label) + L" (" +
        std::to_wstring(dsvr::kSupportedBuilds[i].file_size) + L" bytes)";
  }
  bool size_ok = false;
  for (unsigned i = 0; i < dsvr::kSupportedBuildCount; ++i)
    if (dsvr::kSupportedBuilds[i].file_size ==
        static_cast<unsigned long long>(size.QuadPart))
      size_ok = true;
  log(L"  Size: " + std::to_wstring(size.QuadPart) + L" (supported: " +
      supported + L") " + (size_ok ? L"OK" : L"MISMATCH"));
  log(L"  Hashing 50 MB...");
  std::string sha;
  if (!ShaOf(exe, sha)) {
    log(L"FAIL: hash error.");
    return false;
  }
  log(L"  SHA256: " + dsvr::WideFromUtf8(sha));
  int match = -1;
  for (unsigned i = 0; i < dsvr::kSupportedBuildCount; ++i)
    if (dsvr::kSupportedBuilds[i].file_size ==
            static_cast<unsigned long long>(size.QuadPart) &&
        sha == dsvr::kSupportedBuilds[i].sha256)
      match = static_cast<int>(i);
  log(match >= 0
          ? L"PASS: pinned build supported (" +
                dsvr::WideFromUtf8(dsvr::kSupportedBuilds[match].label) + L")."
          : L"FAIL: unsupported game version. DSVR is qualified against " +
                supported + L" only -- a Steam patch moves the image layout, so "
                L"nothing is assumed about an untested build. Nothing was changed.");
  return match >= 0;
}

// Motion blur (T1): the game preference lives in DarkSouls.ini under
// %LOCALAPPDATA%\FromSoftware\NBGI\DarkSouls ([DisplaySettingFilter] Blur).
std::wstring GameBlurIniPath() {
  wchar_t base[32768]{};
  DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, 32767);
  if (n == 0 || n >= 32767) return {};
  return JoinPath(JoinPath(std::wstring(base, n), L"FromSoftware\\NBGI\\DarkSouls"),
                  L"DarkSouls.ini");
}
bool ReadAllText(const std::wstring& path, std::string& out) {
  FILE* file=nullptr;if(_wfopen_s(&file,path.c_str(),L"rb") || !file)return false;
  char buffer[4096];size_t count=0;out.clear();
  while((count=fread(buffer,1,sizeof(buffer),file)) && out.size()<65536)out.append(buffer,count);
  const bool ok=ferror(file)==0 && out.size()<65536;
  fclose(file);return ok;
}
bool ApplyBlurOnInstall(dsvr::Config& cfg, const std::wstring& config_path, const LogFn& log) {
  std::string status;
  const bool ok = dsvr::InstallBlurPreference(GameBlurIniPath(), config_path, cfg, status);
  log(L"  Motion blur: " + dsvr::WideFromUtf8(status));
  return ok;
}
bool RestoreBlurOnUninstall(const std::wstring& game_dir, const LogFn& log) {
  std::string status;
  const bool ok = dsvr::UninstallBlurPreference(GameBlurIniPath(), JoinPath(game_dir, L"DSVR.toml"), status);
  log(L"  Motion blur: " + dsvr::WideFromUtf8(status));
  return ok;
}

InstallState InstallStateOf(const std::wstring& exe) {
  const std::wstring dir = DirNameOf(exe);
  InstallState st;
  st.has_loader = PathExists(JoinPath(dir, L"dinput8.dll"));
  st.has_core = PathExists(JoinPath(dir, L"DSVR.dll"));
  return st;
}

std::vector<std::wstring> DoctorChecks(const std::wstring& exe,
                                       const LogFn& log) {
  std::vector<std::wstring> lines;
  auto emit = [&](const std::wstring& s) {
    lines.push_back(s);
    log(s);
  };
  emit(L"=== DSVR Doctor ===");
  bool game_ok = VerifyExe(exe, log);
  emit(game_ok ? L"[OK] Game version" : L"[FAIL] Game version (fix this first)");
  emit(IsGameRunning() ? L"[WARN] Game is RUNNING -- close it before install/uninstall"
                       : L"[OK] Game not running");
  std::wstring dir = DirNameOf(exe);
  const InstallState st = InstallStateOf(exe);
  emit(st.installed()
           ? L"[OK] DSVR installed (dinput8.dll + DSVR.dll present)"
           : st.partial()
                 ? L"[WARN] Partial install -- run Install again or Uninstall"
                 : L"[OK] DSVR not installed (vanilla folder)");
  if (PathExists(JoinPath(dir, L"dxgi.dll")))
    emit(L"[INFO] dxgi.dll present (usually ReShade) -- coexists with DSVR, "
         L"no action needed");
  // OpenXR runtime
  {
    HKEY key = nullptr;
    std::wstring runtime = L"(none)";
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Khronos\\OpenXR\\1", 0,
                      KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
      wchar_t buf[1024]{};
      DWORD bytes = sizeof(buf) - sizeof(wchar_t);
      DWORD type = 0;
      if (RegQueryValueExW(key, L"ActiveRuntime", nullptr, &type,
                           (LPBYTE)buf, &bytes) == ERROR_SUCCESS &&
          type == REG_SZ)
        runtime = buf;
      RegCloseKey(key);
    }
    emit(L"[INFO] OpenXR runtime: " + runtime +
         (PathExists(runtime) ? L" (file found)" : L" (check Quest Link / "
                                                    L"Virtual Desktop / SteamVR)"));
  }
  auto saves = FindSaveFiles();
  emit(L"[INFO] Saves found: " + std::to_wstring(saves.size()) +
       L" (backed up automatically on install)");
  bool fw_out = FirewallRuleExists(kRuleOut);
  bool fw_in = FirewallRuleExists(kRuleIn);
  emit(fw_out && fw_in
           ? L"[OK] Firewall block active (extra offline safety)"
           : L"[INFO] Firewall block not active (optional; Steam Offline + "
             L"in-game Offline still required)");
  if (PathExists(JoinPath(dir, L"DSVR.toml"))) {
    dsvr::Config cfg;
    dsvr::ConfigLoad(JoinPath(dir, L"DSVR.toml").c_str(), cfg);
    emit(std::wstring(L"[INFO] DSVR.toml: vr_enabled=") +
         (cfg.vr_enabled ? L"true" : L"false") + L" scale=" +
         std::to_wstring(cfg.render_scale_percent) + L" hand=" +
         (cfg.dominant_hand_left ? L"left" : L"right"));
    {
      std::string ini_text;
      std::wstring blur_current = L"unknown";
      if (ReadAllText(GameBlurIniPath(), ini_text)) {
        const auto pref = dsvr::FindBlurPreference(ini_text);
        if (pref.found) blur_current = dsvr::WideFromUtf8(pref.value);
      }
      emit(std::wstring(L"[INFO] Motion blur: original=") +
           (cfg.motion_blur_original.empty()
                ? std::wstring(L"none")
                : dsvr::WideFromUtf8(cfg.motion_blur_original)) +
           L" forced=" + (cfg.force_disable_motion_blur ? L"true" : L"false") +
           L" current=" + blur_current + L" (game ini)");
    }
  } else {
    emit(L"[INFO] No DSVR.toml yet (created on install)");
  }
  emit(L"=== Doctor done. Send DSVR.log + this output for support. ===");
  return lines;
}

bool FirewallBlocked() {
  return FirewallRuleExists(kRuleOut) && FirewallRuleExists(kRuleIn);
}

bool FirewallSupported(const std::wstring& exe, const LogFn& log, bool add) {
  // Adding the block is idempotent: when both rules already exist there is
    // nothing to prompt or change, so skip straight to success. Only ask
    // when the user has not already accepted (rules absent).
    if (add && FirewallBlocked()) {
    log(L"  Firewall block already active (both rules present) -- no change.");
      return true;
  }
  if (!IsElevated()) {
    log(L"Firewall change needs Administrator. Right-click DSVR-Setup -> "
          L"Run as administrator, then retry. (Optional extra safety -- "
          L"Steam Offline still works without it.)");
    return false;
    }
  bool ok = true;
  if (add) {
      ok = FirewallOne(exe, kRuleOut, L"out", true, log) && ok;
    ok = FirewallOne(exe, kRuleIn, L"in", true, log) && ok;
  } else {
    ok = FirewallOne(exe, kRuleOut, L"out", false, log) && ok;
    ok = FirewallOne(exe, kRuleIn, L"in", false, log) && ok;
  }
  return ok;
}

int DoInstall(const std::wstring& exe, const InstallOptions& opts,
              const LogFn& log, const ConfirmFn& confirm) {
  if (IsGameRunning()) {
    log(L"FAIL: game is running. Close it first (a loaded DLL cannot be replaced).");
    return 1;
  }
  if (!VerifyExe(exe, log)) return 1;
  std::wstring game_dir = DirNameOf(exe);
  std::wstring src = SelfDir();

  // Foreign dinput8 handling: NEVER delete. Offer chain-load rename.
  std::string chain_name;
  std::wstring dest_loader = JoinPath(game_dir, L"dinput8.dll");
  std::wstring src_loader = JoinPath(src, L"dinput8.dll");
  if (PathExists(dest_loader)) {
    std::string a, b;
    bool same = ShaOf(dest_loader, a) && ShaOf(src_loader, b) && a == b;
    if (same) {
      log(L"  dinput8.dll already current (same hash).");
    } else if (LooksLikeOurProxy(dest_loader)) {
      log(L"  Upgrading previous DSVR proxy in place.");
    } else {
      bool move = confirm(
          L"Another dinput8.dll found",
          L"Your game folder already has a dinput8.dll that is NOT DSVR "
          L"(another mod). DSVR will NOT delete it.\n\nMove it to "
          L"dinput8-chain.dll and chain-load it so both mods work? "
          L"(No = cancel install.)",
          false, false);
      if (!move) {
        log(L"Install cancelled: foreign dinput8.dll left untouched. "
            L"Chain-load it by hand (see README) or remove that mod first.");
        return 1;
      }
      std::wstring chain_dest = JoinPath(game_dir, L"dinput8-chain.dll");
      if (PathExists(chain_dest)) {
        log(L"FAIL: " + chain_dest +
            L" already exists. Resolve the conflict by hand, then run "
            L"Install again. Nothing was changed.");
        return 1;
      }
      if (!MoveFileW(dest_loader.c_str(), chain_dest.c_str())) {
        log(L"FAIL: could not move " + dest_loader + L" to " + chain_dest +
            L". Nothing was changed.");
        return 1;
      }
      chain_name = "dinput8-chain.dll";
      log(L"  Moved other proxy to dinput8-chain.dll (chain-load enabled).");
    }
  }

  // Save-profile guard (QoL batch 1, item 3): back up first, always.
  auto saves = FindSaveFiles();
  if (!saves.empty()) {
    log(L"Save guard: found " + std::to_wstring(saves.size()) +
        L" save file(s). Backing up before touching anything...");
  }
  BackupSaves(log);

  bool ack = confirm(
      L"Offline only -- please confirm",
      L"While DSVR is installed you MUST play offline:\n"
      L"Steam Offline Mode + in-game Offline setting.\n"
      L"Never take a save used with DSVR online (ban risk).\n\n"
      L"Install DSVR now?",
      true, false);
  if (!ack) {
    log(L"Install cancelled at the offline confirmation. Saves backup kept.");
    return 1;
  }

  // Config: preserve existing settings, record ack + chain + GUI choices.
  std::wstring cfg_path = JoinPath(game_dir, L"DSVR.toml");
  dsvr::Config cfg;
  bool had_cfg = dsvr::ConfigLoad(cfg_path.c_str(), cfg);
  cfg.offline_only_ack = true;
  if (!chain_name.empty()) cfg.loader_chain_proxy = chain_name;
  if (opts.enable_vr_after) cfg.vr_enabled = true;
  // 0.11.0 migration: controller input used to default off and existing
  // installs carry xr_input_enabled=false, which left Quest controllers
  // producing no input at all. Installing re-enables it; set it false in
  // DSVR.toml afterwards if you prefer a physical pad only.
  cfg.xr_input_enabled = true;
  // 0.12.0 first-person polish migration: camera sits at the head (no
  // forward hover) and the body model (+0x850, identified by the 0.11.0
  // test) is hidden while the weapon-side model stays visible.
  cfg.forward_offset_m = 0.0f;
  cfg.hide_second_model = true;
  // The camera is the tracked eye: the native head shell and the torso/leg
  // shell are hidden by default, while arms and the weapon stay drawn.
  // Whole-model hiding remains an escape hatch, not the default.
  cfg.hide_head = true;
  cfg.hide_torso_and_legs = true;
  cfg.hide_player_model = false;
  if (!ApplyBlurOnInstall(cfg, cfg_path, log)) return 1;
  log(had_cfg ? L"  Kept your DSVR.toml (offline ack recorded)."
              : L"  Wrote DSVR.toml (VR off by default -- enable in setup).");


  const wchar_t* names[] = {L"dinput8.dll", L"DSVR.dll"};
  for (auto* name : names) {
    std::wstring from = JoinPath(src, name);
    std::wstring to = JoinPath(game_dir, name);
    if (!PathExists(from)) {
      log(L"FAIL: " + std::wstring(name) +
          L" not found next to the installer. Re-extract the release zip.");
      return 1;
    }
    if (!CopyFileW(from.c_str(), to.c_str(), FALSE)) {
      log(L"FAIL: copy failed for " + std::wstring(name) +
          L". Run from a writable folder / check antivirus.");
      return 1;
    }
    std::string hs, hd;
    if (!ShaOf(from, hs) || !ShaOf(to, hd) || hs != hd) {
      log(L"FAIL: copied " + std::wstring(name) +
          L" failed hash verification. Delete it and try again.");
      DeleteFileW(to.c_str());
      return 1;
    }
    log(L"  Installed + verified " + std::wstring(name));
  }


  {
    FILE* f = nullptr;
    std::wstring ver_path = JoinPath(game_dir, L"DSVR-VERSION.txt");
    if (_wfopen_s(&f, ver_path.c_str(), L"w") == 0 && f) {
      fprintf(f, "DSVR %s\nOffline play only while installed.\n",
              dsvr::kModVersion);
      fclose(f);
    }
  }

  bool want_fw = confirm(
      L"Extra offline safety?",
      L"Also block DarkSoulsRemastered.exe in Windows Firewall? "
      L"(Recommended extra safety. Needs Administrator; reversible in "
      L"Uninstall. No = skip, Steam Offline still works.)",
      false, false);
  if (want_fw) FirewallSupported(exe, log, true);

  log(L"");
  log(L"DONE. Next: put Steam Offline, launch the game, check DSVR.log.");
  log(L"Before ANY online play: DSVR-Setup --uninstall");
  return 0;
}

int DoUninstall(const std::wstring& exe, bool remove_settings,
                bool unattended, const LogFn& log,
                const ConfirmFn& confirm) {
  (void)unattended;
  if (IsGameRunning()) {
    log(L"FAIL: game is running. Close it first.");
    return 1;
  }
  std::wstring game_dir = DirNameOf(exe);
  if (!RestoreBlurOnUninstall(game_dir, log)) return 1;
  const wchar_t* files[] = {L"dinput8.dll", L"DSVR.dll", L"DSVR.log",
                            L"DSVR-VERSION.txt"};
  for (auto* fn : files) {
    std::wstring p = JoinPath(game_dir, fn);
    if (!PathExists(p)) {
      log(L"  Absent (ok): " + std::wstring(fn));
      continue;
    }
    if (!wcscmp(fn, L"dinput8.dll")) {
      // Only delete a small proxy-sized file. A large dinput8.dll is
      // someone else's mod -- leave it (it may be the chained one).
      LARGE_INTEGER sz{};
      HANDLE f = CreateFileW(p.c_str(), GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                             OPEN_EXISTING, 0, nullptr);
      if (f != INVALID_HANDLE_VALUE) {
        GetFileSizeEx(f, &sz);
        CloseHandle(f);
      }
      if (sz.QuadPart > 512 * 1024) {
        log(L"  KEPT " + std::wstring(fn) +
            L" (too large to be DSVR's proxy -- likely another mod). "
            L"If it is the chained proxy, rename it back by hand.");
        continue;
      }
    }
    if (DeleteFileW(p.c_str()))
      log(L"  Removed " + std::wstring(fn));
    else
      log(L"  FAIL: cannot remove " + std::wstring(fn) +
          L" (close the game, check antivirus).");
  }
  if (PathExists(JoinPath(game_dir, L"dinput8-chain.dll"))) {
    bool restore = confirm(
        L"Restore chained proxy?",
        L"dinput8-chain.dll (the other mod preserved during install) was "
        L"found. Rename it back to dinput8.dll so that mod works again?",
        true, false);
    if (restore) {
      if (MoveFileW(JoinPath(game_dir, L"dinput8-chain.dll").c_str(),
                    JoinPath(game_dir, L"dinput8.dll").c_str()))
        log(L"  Restored chained proxy to dinput8.dll.");
      else
        log(L"  FAIL: could not restore chained proxy; rename by hand.");
    } else {
      log(L"  Kept dinput8-chain.dll (rename to dinput8.dll to re-enable it).");
    }
  }
  if (remove_settings) {
    std::wstring cfg = JoinPath(game_dir, L"DSVR.toml");
    if (PathExists(cfg) && DeleteFileW(cfg.c_str()))
      log(L"  Removed DSVR.toml");
  } else {
    log(L"  Kept DSVR.toml (add --remove-settings to delete it).");
  }
  bool remove_fw = confirm(
      L"Remove firewall block?",
      L"Remove the DSVR firewall block rules? (Yes if you added them and "
      L"want to play online again.)",
      false, false);
  if (remove_fw) FirewallSupported(exe, log, false);
  log(L"DONE. Verify game files in Steam if unsure the folder is clean.");
  return 0;
}

}  // namespace setup

namespace {

void PrintUsage() {
  wprintf(
      L"DSVR-Setup %hs -- Dark Souls: Remastered VR (offline only)\n"
      L"\n"
      L"  (no args)              Open the friendly configurator window\n"
      L"  --gui                  Same as above\n"
      L"  --verify               Check the game build (read-only)\n"
      L"  --doctor               Full health check (read-only)\n"
      L"  --install [--yes]      Install (backs up saves first)\n"
      L"  --uninstall [--yes] [--remove-settings]\n"
      L"  --firewall-block       Block the exe (run as Administrator)\n"
      L"  --firewall-unblock     Remove the block\n"
      L"  --firewall-status      Show block status\n"
      L"  --game PATH            Game folder or exe (auto-detected otherwise)\n"
      L"\n"
      L"Offline play ONLY while installed. Uninstall before going online.\n",
      dsvr::kModVersion);
}

setup::LogFn ConsoleLog() {
  return [](const std::wstring& line) { wprintf(L"%ls\n", line.c_str()); };
}

setup::ConfirmFn ConsoleConfirm(bool unattended) {
  return [unattended](const wchar_t* title, const std::wstring& text,
                      bool default_yes, bool destructive) {
    if (unattended) {
      // Safe prompts auto-accept; anything destructive never does.
      wprintf(L"[%ls] (auto-%ls)\n", title,
              destructive ? L"no" : (default_yes ? L"yes" : L"no"));
      return destructive ? false : default_yes;
    }
    wprintf(L"\n%ls\n%ls\n", title, text.c_str());
    wprintf(default_yes ? L"[Y/n] " : L"[y/N] ");
    std::wstring answer;
    std::getline(std::wcin, answer);
    if (answer.empty()) return default_yes;
    return answer[0] == L'y' || answer[0] == L'Y';
  };
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc <= 1) return RunGui();
  std::wstring cmd, game_override;
  bool remove_settings = false;
  bool unattended = false;
  for (int i = 1; i < argc; i++) {
    std::wstring a = argv[i];
    if (a == L"--verify" || a == L"--install" || a == L"--uninstall" ||
        a == L"--doctor" || a == L"--gui" || a == L"--firewall-block" ||
        a == L"--firewall-unblock" || a == L"--firewall-status" ||
        a == L"--help" || a == L"-h" || a == L"/?")
      cmd = a;
    else if (a == L"--game" && i + 1 < argc)
      game_override = argv[++i];
    else if (a == L"--remove-settings")
      remove_settings = true;
    else if (a == L"--yes" || a == L"-y")
      unattended = true;
    else {
      wprintf(L"Unknown arg: %ls\n\n", a.c_str());
      PrintUsage();
      return 1;
    }
  }
  if (cmd == L"--gui") return RunGui();
  if (cmd.empty() || cmd == L"--help" || cmd == L"-h" || cmd == L"/?") {
    PrintUsage();
    return cmd.empty() ? 1 : 0;
  }
  std::wstring exe = setup::FindGameExe(game_override);
  if (exe.empty()) {
    wprintf(L"FAIL: game not found. Pass --game \"...\\DARK SOULS REMASTERED\"\n");
    return 1;
  }
  auto log = ConsoleLog();
  if (cmd == L"--verify")
    return setup::VerifyExe(exe, log) ? 0 : 1;
  if (cmd == L"--doctor") {
    setup::DoctorChecks(exe, log);
    return 0;
  }
  if (cmd == L"--firewall-status") {
    bool blocked = setup::FirewallBlocked();
    wprintf(L"Firewall block: %ls\n",
            blocked ? L"ACTIVE (both rules present)"
                    : L"not active (optional -- Steam Offline still required)");
    return blocked ? 0 : 1;
  }
  if (cmd == L"--firewall-block")
    return setup::FirewallSupported(exe, log, true) ? 0 : 1;
  if (cmd == L"--firewall-unblock")
    return setup::FirewallSupported(exe, log, false) ? 0 : 1;
  if (cmd == L"--install") {
    setup::InstallOptions opts;
    opts.unattended = unattended;
    return setup::DoInstall(exe, opts, log, ConsoleConfirm(unattended));
  }
  if (cmd == L"--uninstall")
    return setup::DoUninstall(exe, remove_settings, unattended, log,
                              ConsoleConfirm(unattended));
  return 1;
}
