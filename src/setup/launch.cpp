// See launch.h. This is the gui.cpp LaunchGame process-start path moved
// verbatim into the UI-free layer; the GUI keeps its own prompts and logging
// and now calls here, so the --json launch verb reuses the same routing.
#include "launch.h"

#include <windows.h>

#include "setup_internal.h"

namespace setup {

std::wstring SteamClientExePath() {
  wchar_t pf86[32768]{};
  if (GetEnvironmentVariableW(L"ProgramFiles(x86)", pf86, 32767) > 0) {
    const std::wstring cand = std::wstring(pf86) + L"\\Steam\\Steam.exe";
    DWORD a = GetFileAttributesW(cand.c_str());
    if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY))
      return cand;
  }
  const std::wstring fallback = L"C:\\Program Files (x86)\\Steam\\Steam.exe";
  DWORD a = GetFileAttributesW(fallback.c_str());
  if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY))
    return fallback;
  return {};
}

LaunchResult LaunchGameProcess(const std::wstring& exe, bool prefer_steam) {
  LaunchResult result;
  const std::wstring steam = prefer_steam ? SteamClientExePath() : std::wstring();
  STARTUPINFOW start{};
  start.cb = sizeof(start);
  PROCESS_INFORMATION process{};
  std::wstring command;
  LPCWSTR app = nullptr;
  if (!steam.empty()) {
    // steam://rungameid/570940 activates Dark Souls: Remastered through the
    // client, which is what Steam init requires when Steam is already open.
    command = L"\"" + steam + L"\" steam://rungameid/570940";
    app = steam.c_str();
    result.via_steam = true;
  } else {
    command = L"\"" + exe + L"\"";
    app = exe.c_str();
  }
  if (!CreateProcessW(app, command.data(), nullptr, nullptr, FALSE, 0,
                      nullptr, DirNameOf(exe).c_str(), &start, &process)) {
    result.win32_error = GetLastError();
    return result;
  }
  CloseHandle(process.hThread);
  // Grant the game the right to take foreground once its window exists, so
  // the player's keyboard/controller focus lands on the game instead of
  // staying on the launcher. The game asks for foreground itself at startup;
  // without this grant Windows refuses that request.
  AllowSetForegroundWindow(process.dwProcessId);
  CloseHandle(process.hProcess);
  result.started = true;
  result.pid = process.dwProcessId;
  return result;
}

}  // namespace setup
