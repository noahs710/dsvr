#pragma once
// Machine-readable stdout JSON for `DSVR-Setup.exe --json <verb>`
// Pure builders: the caller collects
// the data with the existing UI-free setup_internal.h calls, this file only
// renders it, so the documents are assertable offline without a game folder.
#include <string>
#include <vector>

#include "../common/config.h"
#include "setup_internal.h"

namespace setup {

// One UTF-8 JSON string literal (with quotes) for a wide string.
std::string JsonString(const std::wstring& text);

// status → {exe, game_running, install_state, fps_unlock_state, headset_hz,
// version} — exactly the structs setup_internal.h exposes.
std::string StatusJson(const std::wstring& exe, bool game_running,
                       const InstallState& install, const FpsUnlockState& fps,
                       int headset_hz, const char* version);

// doctor → {checks: [...], ok} reusing DoctorChecks' emitted lines;
// ok is false iff any line carries "[FAIL]" (WARNs stay informational).
std::string DoctorJson(const std::vector<std::wstring>& checks);

// verify → {ok, sha256, supported}.
std::string VerifyJson(bool supported, const std::string& sha256);

// settings-index: the single C++ settings-row table. Every config key must be
// reachable through it (plan section 4). gui.cpp's AdvancedSettingsIndex copy
// retires onto this table when that file is free (phase 4); until then the
// test asserts this table's shape and the store-facing rows stay here.
struct SettingsRow {
  const wchar_t* group;
  const wchar_t* label;
  const wchar_t* key;
};
const std::vector<SettingsRow>& SettingsIndex();
std::string SettingsIndexJson();  // {rows: [{group, label, key}, ...]}

// preset-index: the shared user-preset table (core/user_presets.h) as
// {presets: [{id, label, detail, hardware_fit}]}. The launcher renders its
// preset cards from this, so the UI and the engine cannot drift apart.
std::string PresetIndexJson();

// ---------------------------------------------------------------------------
// Phase 3 (settings parity): the same key names SettingsIndex exposes are the
// write/read keys of the config round-trip, so the generated launcher form and
// DSVR.toml can never drift apart. ConfigApplyKeyValue takes one "key=value"
// payload (exactly what `--json config-set` receives on argv), validates the
// key against the table, and refuses values the schema or ConfigValid would
// reject; callers must run ConfigValid on the whole Config before saving.
// ---------------------------------------------------------------------------

// config-get: one JSON object keyed by every table key, with the same value
// spellings DSVR.toml uses (true/false, numbers, "left"/"head"/"standing"...).
std::string ConfigGetJson(const dsvr::Config& cfg);

// Apply one "key=value" pair. Returns false and fills `error` (human text,
// goes straight into the JSON error field) when the key is unknown, the value
// is malformed, or the key is Setup-managed and must not be edited here.
bool ConfigApplyKeyValue(dsvr::Config& cfg, const std::wstring& key_value,
                         std::wstring& error);

// launch / launch-native: {ok, pid, via_steam} or {ok:false, error}.
std::string LaunchJson(bool ok, unsigned long pid, bool via_steam,
                       const wchar_t* error);

}  // namespace setup
