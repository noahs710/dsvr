#pragma once
#include <string>
#include <functional>

#include "../common/config.h"

namespace dsvr {

// Diagnostics hotkeys (QoL batch 1, item 5):
//  - Log-marker key (default F9): writes a timestamped marker + best-effort
//    player/camera snapshot to DSVR.log. Makes bug reports actionable.
//  - Reload key (default F7): re-reads DSVR.toml without restarting, so
//    comfort/combat tuning can be A/B tested in one session.
// Polling runs on our own thread (~25 Hz, foreground only) and only ever
// logs + swaps config. It never writes game memory.
void DiagnosticsStart(const Config& initial, std::wstring config_path);
void DiagnosticsStop();
// Diagnostic writer only; preserves the process's startup observer permission.
bool DiagnosticsReloadConfig();
// Same marker + snapshot + capture as the F9 hotkey, for controller chords.
void DiagnosticsLogMarker();
// Diagnostic writer only. Observer hooks activate once on a foreground capture
// command, never during initial DLL startup. Failed activation is not retried.
bool DiagnosticsBeginCapture();

// Thread-safe copy of the live config (updated by F7 reloads).
Config DiagnosticsCurrentConfig();
// Queue UI changes for the diagnostic writer; no disk I/O on render callbacks.
bool DiagnosticsRequestConfig(const Config& config, bool save);
void DiagnosticsRequestLogExport();
std::string DiagnosticsSettingsStatus();
// Bounded menu file/settings commands. Never use for native gameplay calls.
bool DiagnosticsRequestCommand(const char* label, std::function<bool()> command);

}  // namespace dsvr
