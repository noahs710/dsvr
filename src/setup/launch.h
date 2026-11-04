#pragma once
// Steam-routed game launch shared by the GUI (gui.cpp) and the --json seam
// (`DSVR-Setup.exe --json launch`). One owner for the steam://rungameid route
// and the foreground grant, so the launcher front-ends can never disagree.
#include <string>

namespace setup {

struct LaunchResult {
  bool started = false;
  bool via_steam = false;
  unsigned long pid = 0;
  unsigned long win32_error = 0;
};

// Empty when no Steam client is installed in the standard locations.
std::wstring SteamClientExePath();

// prefer_steam: launch through the Steam client (steam://rungameid/570940),
// which is what Steam init requires when the client is already open; falls
// back to the raw exe when no client is found. Always grants the game the
// foreground once its window exists.
LaunchResult LaunchGameProcess(const std::wstring& exe, bool prefer_steam);

}  // namespace setup
