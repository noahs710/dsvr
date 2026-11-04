// The Setup --json seam used by the Electron launcher.
// json.cpp is pure string building, so these checks pin the exact documents
// the launcher will parse, with no game folder, device or install state.
#include <cstdio>
#include <string>

#include "../src/setup/json.h"

#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main() {
  using namespace setup;

  // Escaping: backslashes, quotes and control characters all fold into a
  // valid JSON string literal (paths are the common case).
  CHECK(JsonString(L"C:\\game\\DSVR.exe") == "\"C:\\\\game\\\\DSVR.exe\"");
  CHECK(JsonString(L"a\"b") == "\"a\\\"b\"");
  CHECK(JsonString(L"a\nb") == "\"a\\nb\"");

  // status: exactly the setup_internal.h structs, one document.
  InstallState install;  // struct defaults to vanilla (nothing present)
  FpsUnlockState fps;    // default: nothing installed
  const std::string status = StatusJson(L"C:\\g\\DarkSoulsRemastered.exe",
                                        false, install, fps, 120, "0.16.0-test");
  const std::string expected_status =
      "{\"exe\":\"C:\\\\g\\\\DarkSoulsRemastered.exe\","
      "\"game_running\":false,"
      "\"install_state\":{\"has_loader\":false,\"has_core\":false,"
      "\"installed\":false,\"partial\":false},"
      "\"fps_unlock_state\":{\"chain_present\":false,\"ini_present\":false,"
      "\"proxy_set\":false,\"installed\":false,\"required\":true},"
      "\"headset_hz\":120,\"version\":\"0.16.0-test\"}";
  if (status != expected_status) {
    std::printf("status mismatch\n expected: %s\n   actual: %s\n",
                expected_status.c_str(), status.c_str());
    return 1;
  }
  install.has_loader = true;
  install.has_core = true;
  CHECK(StatusJson(L"x", true, install, fps, 90, "v").find(
            "\"install_state\":{\"has_loader\":true,\"has_core\":true,"
            "\"installed\":true,\"partial\":false}") != std::string::npos);
  InstallState half = install;
  half.has_core = false;
  CHECK(StatusJson(L"x", false, half, fps, 0, "v").find(
            "\"partial\":true") != std::string::npos);

  // doctor: ok is false iff any emitted line carries [FAIL]; WARN stays soft.
  CHECK(DoctorJson({L"[OK] Game version", L"[WARN] Game is RUNNING"}).find(
            "\"ok\":true") != std::string::npos);
  CHECK(DoctorJson({L"[OK] Game version",
                    L"[FAIL] Game version (fix this first)"}).find(
            "\"ok\":false") != std::string::npos);
  CHECK(DoctorJson({L"[OK] a", L"[WARN] b"}) ==
        "{\"checks\":[\"[OK] a\",\"[WARN] b\"],\"ok\":true}");

  // verify: the sha rides along so the shell can show it without re-hashing.
  CHECK(VerifyJson(true, "ab12") ==
        "{\"ok\":true,\"sha256\":\"ab12\",\"supported\":true}");
  CHECK(VerifyJson(false, "").find("\"sha256\":\"\"") != std::string::npos);

  // settings-index: one C++ table, the same rows the human index shows.
  const auto& rows = SettingsIndex();
  CHECK(rows.size() > 60);
  bool seen_theater = false, seen_grid = false, seen_playstyle = false,
       seen_proxy = false;
  for (size_t i = 0; i < rows.size(); ++i) {
    CHECK(rows[i].group != nullptr && rows[i].label != nullptr &&
          rows[i].key != nullptr);
    for (size_t j = 0; j < i; ++j)
      CHECK(rows[j].key != rows[i].key);  // keys unique (pointer-distinct rows)
    if (std::wstring(rows[i].key) == L"theater_override") seen_theater = true;
    if (std::wstring(rows[i].key) == L"theater_floor_grid") seen_grid = true;
    if (std::wstring(rows[i].key) == L"play_style") seen_playstyle = true;
    if (std::wstring(rows[i].key) == L"chain_proxy") seen_proxy = true;
  }
  CHECK(seen_theater && seen_grid && seen_playstyle && seen_proxy);
  // Key names must be distinct strings, not just distinct pointers.
  for (size_t i = 0; i < rows.size(); ++i)
    for (size_t j = i + 1; j < rows.size(); ++j)
      CHECK(std::wstring(rows[i].key) != std::wstring(rows[j].key));

  const std::string index = SettingsIndexJson();
  CHECK(index.rfind("{\"rows\":[", 0) == 0);
  CHECK(index.find("\"key\":\"theater_floor_grid\"") != std::string::npos);
  CHECK(index.find("\"group\":\"Combat and haptics\"") != std::string::npos);
  CHECK(index.back() == '}');

  // preset-index: the shared preset table, including the hardware-fit flag.
  const std::string presets = PresetIndexJson();
  CHECK(presets.rfind("{\"presets\":[", 0) == 0);
  CHECK(presets.find("\"id\":\"comfort\"") != std::string::npos);
  CHECK(presets.find("\"id\":\"balanced\"") != std::string::npos);
  CHECK(presets.find("\"id\":\"sharper\"") != std::string::npos);
  CHECK(presets.find("\"id\":\"match\"") != std::string::npos);
  CHECK(presets.find("\"hardware_fit\":true") != std::string::npos);
  CHECK(presets.find("\"hardware_fit\":false") != std::string::npos);
  CHECK(presets.back() == '}');

  // ---- phase 3: config get/set round-trip -------------------------------
  // Every SettingsIndex row must be a key the config verbs accept, so the
  // generated launcher form and DSVR.toml can never drift apart.
  for (const auto& row : rows) {
    dsvr::Config cfg;
    std::wstring error;
    ConfigApplyKeyValue(cfg, std::wstring(row.key) + L"=1", error);
    CHECK(error.find(L"unknown setting") == std::wstring::npos);
  }

  // config-get on defaults: value spellings are the DSVR.toml spellings.
  dsvr::Config defaults;
  const std::string get = ConfigGetJson(defaults);
  CHECK(get.rfind("{\"ok\":true,\"settings\":{", 0) == 0);
  CHECK(get.find("\"play_style\":\"standing\"") != std::string::npos);
  CHECK(get.find("\"dominant_hand\":\"right\"") != std::string::npos);
  CHECK(get.find("\"render_scale_percent\":100") != std::string::npos);
  CHECK(get.find("\"theater_floor_grid\":true") != std::string::npos);
  CHECK(get.find("motion_blur_original") == std::string::npos);  // read-only
  CHECK(get.back() == '}');

  // Apply good values, then reject malformed ones.
  std::wstring error;
  CHECK(ConfigApplyKeyValue(defaults, L"render_scale_percent=150", error));
  CHECK(ConfigApplyKeyValue(defaults, L"dominant_hand=left", error));
  CHECK(ConfigApplyKeyValue(defaults, L"vignette_strength=0.5", error));
  CHECK(ConfigApplyKeyValue(defaults, L"play_style=seated", error));
  const std::string changed = ConfigGetJson(defaults);
  CHECK(changed.find("\"render_scale_percent\":150") != std::string::npos);
  CHECK(changed.find("\"dominant_hand\":\"left\"") != std::string::npos);
  CHECK(changed.find("\"play_style\":\"seated\"") != std::string::npos);
  CHECK(!ConfigApplyKeyValue(defaults, L"render_scale_percent=wide", error));
  CHECK(!ConfigApplyKeyValue(defaults, L"no_such_setting=1", error));
  CHECK(!ConfigApplyKeyValue(defaults, L"=novalue", error));
  CHECK(!ConfigApplyKeyValue(defaults, L"play_style=flying", error));
  CHECK(!ConfigApplyKeyValue(defaults, L"motion_blur_original=1", error));
  CHECK(ConfigApplyKeyValue(defaults, L"dominant_hand=left", error));  // idempotent

  // launch: {ok, pid, via_steam} on success, {ok:false, error} on failure.
  CHECK(LaunchJson(true, 4242, true, nullptr) ==
        "{\"ok\":true,\"pid\":4242,\"via_steam\":true}");
  CHECK(LaunchJson(false, 0, false, L"game_already_running") ==
        "{\"ok\":false,\"error\":\"game_already_running\"}");

  std::printf("setup_json: all checks pass (%zu rows)\n", rows.size());
  return 0;
}
