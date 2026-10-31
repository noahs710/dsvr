#pragma once
// Shared installer logic used by both the CLI (setup.cpp) and the GUI
// (gui.cpp). Everything here is UI-free: callers pass a logger + a confirm
// callback, so console prompts and MessageBox prompts share one code path.
#include <functional>
#include <string>
#include <vector>

#include "../core/perf_fit.h"

namespace setup {

using LogFn = std::function<void(const std::wstring& line)>;
// Return true = proceed/yes, false = stop/no. `default_yes` only matters for
// unattended mode: safe prompts default to it, destructive ones never do.
using ConfirmFn =
    std::function<bool(const wchar_t* title, const std::wstring& text,
                       bool default_yes, bool destructive)>;

std::wstring JoinPath(const std::wstring& dir, const std::wstring& file);
bool PathExists(const std::wstring& p);
std::wstring DirNameOf(const std::wstring& path);
std::wstring FindGameExe(const std::wstring& override_path);
bool IsGameRunning();
bool VerifyExe(const std::wstring& exe, const LogFn& log);
std::vector<std::wstring> DoctorChecks(const std::wstring& exe,
                                       const LogFn& log);

// What the game folder currently holds. DoctorChecks and the GUI both report
// this one struct, so they can never disagree about the install state.
struct InstallState {
  bool has_loader = false;  // dinput8.dll present
  bool has_core = false;    // DSVR.dll present
  bool installed() const { return has_loader && has_core; }
  bool partial() const { return has_loader != has_core; }
};
InstallState InstallStateOf(const std::wstring& exe);

dsvr::HardwareSpec ProbeHardware();  // fail-soft: unknown fields stay zero (lowest tier)

bool FirewallSupported(const std::wstring& exe, const LogFn& log, bool add);
bool FirewallBlocked();  // true when both DSVR block rules exist

// ---------------------------------------------------------------------------
// DSR-FPS-Unlock dependency (the chain-loaded frame-rate owner).
//
// DSVR never caps the game itself; high-rate pacing belongs to this external
// MIT dependency, loaded through the loader's chain_proxy. The pin below is the
// exact GitHub release asset this build installs: the URL is allow-listed to one
// host/path, and both the ZIP and the extracted DLL must hash to the pinned
// digests before anything is written next to the game.
// ---------------------------------------------------------------------------
struct FpsUnlockPin {
  const wchar_t* url = L"";
  const wchar_t* zip_sha256 = L"";
  const wchar_t* dll_sha256 = L"";
  const wchar_t* chain_file = L"dinput8-chain.dll";  // installed name
  const wchar_t* ini_file = L"DSR-FPS-Unlock.ini";
};
const FpsUnlockPin& FpsUnlockPinInfo();

// True only for the exact pinned https://github.com release asset. Anything
// else (http, a mirror, a redirect host other than the release CDN, a
// different path) is refused before a byte is fetched.
bool FpsUnlockUrlAllowed(const std::wstring& url);

// What the game folder holds for the dependency right now.
struct FpsUnlockState {
  bool chain_present = false;  // dinput8-chain.dll next to the exe
  bool ini_present = false;    // DSR-FPS-Unlock.ini next to the exe
  bool proxy_set = false;      // DSVR.toml chain_proxy names the chain file
  bool installed() const { return chain_present && ini_present && proxy_set; }
};
FpsUnlockState FpsUnlockStateOf(const std::wstring& exe);

// Download the pinned asset, verify it, and install it as the chain proxy.
// Refuses while the game is running, snapshots anything it is about to
// overwrite into %TEMP% first, and never touches the root dinput8.dll.
// Returns 0 on success, non-zero on failure (reasons go to `log`).
int InstallFpsUnlock(const std::wstring& exe, const LogFn& log,
                     const ConfirmFn& confirm);

// Download the user-requested TPUP 1.5 release to the Windows Downloads folder
// and extract it there. The tool is never launched and game files are untouched.
const wchar_t* TextureToolsUrl();
bool TextureToolsUrlAllowed(const std::wstring& url);
int DownloadTextureTools(const LogFn& log, const ConfirmFn& confirm,
                         std::wstring& extracted_folder);

struct InstallOptions {
  bool unattended = false;  // --yes: skip safe prompts, never auto-destroy
  bool enable_vr_after = false;  // GUI: tick "enable VR" before installing
};
int DoInstall(const std::wstring& exe, const InstallOptions& opts,
              const LogFn& log, const ConfirmFn& confirm);
int DoUninstall(const std::wstring& exe, bool remove_settings,
                bool unattended, const LogFn& log,
                const ConfirmFn& confirm);

}  // namespace setup
