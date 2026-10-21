#pragma once
// Shared installer logic used by both the CLI (setup.cpp) and the GUI
// (gui.cpp). Everything here is UI-free: callers pass a logger + a confirm
// callback, so console prompts and MessageBox prompts share one code path.
#include <functional>
#include <string>
#include <vector>

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

bool FirewallSupported(const std::wstring& exe, const LogFn& log, bool add);
bool FirewallBlocked();  // true when both DSVR block rules exist

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
