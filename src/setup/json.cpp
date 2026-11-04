// Machine-readable stdout JSON for `DSVR-Setup.exe --json <verb>`
// Everything here is pure string
// building over the structs setup_internal.h already exposes; the caller in
// setup.cpp collects the live data, so the Electron shell (and the offline
// test) see exactly one document per invocation with no human text mixed in.
#include "json.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <optional>

#include "../common/config.h"
#include "../core/user_presets.h"

namespace setup {

std::string JsonString(const std::wstring& text) {
  int need = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(),
                                 nullptr, 0, nullptr, nullptr);
  std::string utf8(need > 0 ? need : 0, '\0');
  if (need > 0)
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &utf8[0],
                        need, nullptr, nullptr);
  std::string out = "\"";
  for (unsigned char c : utf8) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += (char)c;
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else if (c == '\t') {
      out += "\\t";
    } else if (c < 0x20) {
      char esc[8];
      std::snprintf(esc, sizeof esc, "\\u%04x", c);
      out += esc;
    } else {
      out += (char)c;
    }
  }
  out += '"';
  return out;
}

namespace {
const char* B(bool b) { return b ? "true" : "false"; }
}  // namespace

// ---------------------------------------------------------------------------
// Phase 3 config round-trip: one table owns every editable key, so the
// generated launcher form, `--json config-get/set` and DSVR.toml share one
// schema. Key spellings are exactly the SettingsIndex keys (= DSVR.toml keys);
// tests/test_setup_json.cpp asserts every SettingsIndex row has an entry, so
// adding a setting touches this table and nothing else.
// ---------------------------------------------------------------------------
namespace {

enum FieldKind { kBool, kInt, kFloat, kStr, kDominant, kOrientation,
                 kController, kReadOnly };

struct ConfigField {
  const wchar_t* key;
  FieldKind kind;
  bool dsvr::Config::*bp = nullptr;
  int dsvr::Config::*ip = nullptr;
  float dsvr::Config::*fp = nullptr;
  std::string dsvr::Config::*sp = nullptr;
};

const std::vector<ConfigField>& ConfigFields() {
  using dsvr::Config;
  static const std::vector<ConfigField> fields = [] {
    std::vector<ConfigField> f;
    auto add = [&f](const wchar_t* key, FieldKind kind, auto member) {
      ConfigField e;
      e.key = key;
      e.kind = kind;
      if constexpr (std::is_same_v<decltype(member), bool Config::*>)
        e.bp = member;
      else if constexpr (std::is_same_v<decltype(member), int Config::*>)
        e.ip = member;
      else if constexpr (std::is_same_v<decltype(member), float Config::*>)
        e.fp = member;
      else if constexpr (std::is_same_v<decltype(member),
                                        std::string Config::*>)
        e.sp = member;
      f.push_back(e);
    };
    // Launcher and VR
    add(L"chain_proxy", kStr, &Config::loader_chain_proxy);
    add(L"vr_enabled", kBool, &Config::vr_enabled);
    add(L"reshade_host", kBool, &Config::reshade_host);
    add(L"render_scale_percent", kInt, &Config::render_scale_percent);
    add(L"desktop_present", kBool, &Config::desktop_present);
    add(L"comfort_vignette", kBool, &Config::comfort_vignette);
    add(L"vignette_strength", kFloat, &Config::vignette_strength);
    add(L"fov_scale", kFloat, &Config::fov_scale);
    add(L"snap_turn", kBool, &Config::snap_turn);
    add(L"snap_turn_degrees", kInt, &Config::snap_turn_degrees);
    add(L"smooth_turn", kBool, &Config::smooth_turn);
    add(L"smooth_turn_speed_dps", kFloat, &Config::smooth_turn_speed_dps);
    add(L"headlook_enabled", kBool, &Config::headlook_enabled);
    add(L"gameplay_theater", kBool, &Config::gameplay_theater);
    add(L"mono_screen_fov_deg", kFloat, &Config::mono_screen_fov_deg);
    add(L"theater_override", kBool, &Config::theater_override);
    add(L"theater_floor_grid", kBool, &Config::theater_floor_grid);
    add(L"menu_distance_m", kFloat, &Config::menu_distance_m);
    add(L"menu_reading_fov_deg", kFloat, &Config::menu_reading_fov_deg);
    add(L"force_disable_motion_blur", kBool, &Config::force_disable_motion_blur);
    add(L"stereo_enabled", kBool, &Config::stereo_enabled);
    add(L"afr_enabled", kBool, &Config::afr_enabled);
    add(L"aa_mode", kInt, &Config::aa_mode);
    add(L"cas_enabled", kBool, &Config::cas_enabled);
    add(L"cas_strength", kFloat, &Config::cas_strength);
    // Controls and movement
    add(L"dominant_hand", kDominant, nullptr);
    add(L"movement_orientation", kOrientation, nullptr);
    add(L"movement_controller", kController, nullptr);
    add(L"play_style", kStr, &Config::play_style);
    add(L"arm_swing_sprint", kBool, &Config::arm_swing_sprint);
    add(L"arm_swing_stride_min", kFloat, &Config::arm_swing_stride_min);
    add(L"arm_swing_stride_max", kFloat, &Config::arm_swing_stride_max);
    add(L"arm_swing_quiet_max", kFloat, &Config::arm_swing_quiet_max);
    add(L"arm_swing_alternations", kInt, &Config::arm_swing_alternations);
    add(L"arm_swing_hold_ms", kInt, &Config::arm_swing_hold_ms);
    add(L"reach_interact", kBool, &Config::reach_interact);
    add(L"reach_forward_min_m", kFloat, &Config::reach_forward_min_m);
    add(L"reach_forward_max_m", kFloat, &Config::reach_forward_max_m);
    add(L"reach_speed_min", kFloat, &Config::reach_speed_min);
    add(L"reach_dominance_min", kFloat, &Config::reach_dominance_min);
    add(L"reach_quiet_max", kFloat, &Config::reach_quiet_max);
    add(L"reach_settle_ms", kInt, &Config::reach_settle_ms);
    add(L"reach_press_ms", kInt, &Config::reach_press_ms);
    add(L"reach_cooldown_ms", kInt, &Config::reach_cooldown_ms);
    add(L"hotkeys_enabled", kBool, &Config::hotkeys_enabled);
    add(L"xr_input_enabled", kBool, &Config::xr_input_enabled);
    add(L"bind_profile_quest3", kBool, &Config::bind_profile_quest3);
    add(L"bind_profile_index", kBool, &Config::bind_profile_index);
    add(L"bind_profile_wmr", kBool, &Config::bind_profile_wmr);
    add(L"bind_profile_simple", kBool, &Config::bind_profile_simple);
    add(L"binding_live_reattach", kBool, &Config::binding_live_reattach);
    add(L"log_marker_vk", kInt, &Config::log_marker_vk);
    add(L"config_reload_vk", kInt, &Config::config_reload_vk);
    // Camera and body
    add(L"eye_height_override", kBool, &Config::eye_height_override);
    add(L"eye_height", kFloat, &Config::eye_height);
    add(L"seated_height_offset_m", kFloat, &Config::seated_height_offset_m);
    add(L"world_scale", kFloat, &Config::world_scale);
    add(L"avatar_scale", kFloat, &Config::avatar_scale);
    add(L"forward_offset_m", kFloat, &Config::forward_offset_m);
    add(L"eye_clearance_m", kFloat, &Config::eye_clearance_m);
    add(L"hide_player_model", kBool, &Config::hide_player_model);
    add(L"hide_head", kBool, &Config::hide_head);
    add(L"hide_torso_and_legs", kBool, &Config::hide_torso_and_legs);
    add(L"forearm_only", kBool, &Config::forearm_only);
    add(L"hide_ui_in_vr", kBool, &Config::hide_ui_in_vr);
    add(L"hide_second_model", kBool, &Config::hide_second_model);
    add(L"embodiment_fit", kStr, &Config::embodiment_fit);
    // Combat and haptics
    add(L"motion_attacks", kBool, &Config::motion_attacks);
    add(L"swing_speed_min", kFloat, &Config::swing_speed_min);
    add(L"swing_damage_gain", kFloat, &Config::swing_damage_gain);
    add(L"thrust_enabled", kBool, &Config::thrust_enabled);
    add(L"thrust_speed_min", kFloat, &Config::thrust_speed_min);
    add(L"haptics", kBool, &Config::haptics);
    add(L"arm_ik_enabled", kBool, &Config::arm_ik_enabled);
    add(L"arm_ik_log_only", kBool, &Config::arm_ik_log_only);
    add(L"arm_ik_block_animation", kBool, &Config::arm_ik_block_animation);
    add(L"grip_roll_correction_deg", kFloat, &Config::grip_roll_correction_deg);
    add(L"contact_speed_min", kFloat, &Config::contact_speed_min);
    add(L"rehit_window_ms", kInt, &Config::rehit_window_ms);
    add(L"physical_attacks", kBool, &Config::physical_attacks);
    add(L"swing_attack_speed_min", kFloat, &Config::swing_attack_speed_min);
    add(L"swing_attack_heavy_min", kFloat, &Config::swing_attack_heavy_min);
    add(L"physical_defence", kBool, &Config::physical_defence);
    add(L"guard_settle_ms", kInt, &Config::guard_settle_ms);
    add(L"guard_flick_speed_min", kFloat, &Config::guard_flick_speed_min);
    add(L"guard_parry_press_ms", kInt, &Config::guard_parry_press_ms);
    add(L"guard_parry_cooldown_ms", kInt, &Config::guard_parry_cooldown_ms);
    add(L"combat_test_bench", kBool, &Config::combat_test_bench);
    add(L"combat_test_target_m", kFloat, &Config::combat_test_target_m);
    add(L"weapon_separation_min_m", kFloat, &Config::weapon_separation_min_m);
    add(L"weapon_mass_table_path", kStr, &Config::weapon_mass_table_path);
    add(L"torque_response", kFloat, &Config::torque_response);
    add(L"halfsword_envelope_m", kFloat, &Config::halfsword_envelope_m);
    add(L"deflect_amplitude", kFloat, &Config::deflect_amplitude);
    add(L"deflect_duration_ms", kInt, &Config::deflect_duration_ms);
    // Diagnostics and safety
    add(L"verbose_logging", kBool, &Config::verbose_logging);
    add(L"observer_only", kBool, &Config::observer_only);
    add(L"combat_trace_only", kBool, &Config::combat_trace_only);
    add(L"autonomous_session_capture", kBool,
        &Config::autonomous_session_capture);
    add(L"dev_mode", kBool, &Config::dev_mode);
    add(L"offline_only_ack", kBool, &Config::offline_only_ack);
    add(L"motion_blur_original", kReadOnly, nullptr);
    return f;
  }();
  return fields;
}

const ConfigField* FindField(const std::wstring& key) {
  for (const auto& f : ConfigFields())
    if (key == f.key) return &f;
  return nullptr;
}

std::string Float6(float v) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.6g", v);
  return buf;
}

const wchar_t* EnumValue(const dsvr::Config& cfg, FieldKind kind) {
  using namespace dsvr;
  switch (kind) {
    case kDominant: return cfg.dominant_hand_left ? L"left" : L"right";
    case kOrientation:
      return cfg.movement_orientation == MovementOrientation::Controller
                 ? L"controller" : L"head";
    default:
      return cfg.movement_controller == MovementController::Left ? L"left"
             : cfg.movement_controller == MovementController::Right ? L"right"
             : L"non_dominant";
  }
}

}  // namespace

std::string ConfigGetJson(const dsvr::Config& cfg) {
  std::string out = "{\"ok\":true,\"settings\":{";
  for (size_t i = 0; i < ConfigFields().size(); ++i) {
    const auto& f = ConfigFields()[i];
    if (f.kind == kReadOnly) continue;
    if (out.back() != '{') out += ',';
    out += JsonString(f.key) + ":";
    switch (f.kind) {
      case kBool: out += B(cfg.*(f.bp)); break;
      case kInt: out += std::to_string(cfg.*(f.ip)); break;
      case kFloat: out += Float6(cfg.*(f.fp)); break;
      case kStr:
        out += JsonString(dsvr::WideFromUtf8(cfg.*(f.sp))); break;
      case kDominant: case kOrientation: case kController:
        out += JsonString(EnumValue(cfg, f.kind)); break;
      default: break;
    }
  }
  out += "}}";
  return out;
}

bool ConfigApplyKeyValue(dsvr::Config& cfg, const std::wstring& key_value,
                         std::wstring& error) {
  error.clear();
  const auto eq = key_value.find(L'=');
  if (eq == std::wstring::npos || eq == 0) {
    error = L"expected key=value, got: " + key_value;
    return false;
  }
  const std::wstring key = key_value.substr(0, eq);
  const std::wstring value = key_value.substr(eq + 1);
  const ConfigField* f = FindField(key);
  if (!f) {
    error = L"unknown setting: " + key;
    return false;
  }
  if (f->kind == kReadOnly) {
    error = L"Setup-managed, not editable here: " + key;
    return false;
  }
  auto whole_int = [&]() -> std::optional<long long> {
    if (value.empty()) return std::nullopt;
    wchar_t* end = nullptr;
    const long long v = std::wcstoll(value.c_str(), &end, 0);
    if (*end != L'\0') return std::nullopt;
    return v;
  };
  switch (f->kind) {
    case kBool:
      if (value == L"true") cfg.*(f->bp) = true;
      else if (value == L"false") cfg.*(f->bp) = false;
      else { error = L"needs true or false: " + key; return false; }
      return true;
    case kInt: {
      const auto v = whole_int();
      if (!v) { error = L"needs a whole number: " + key; return false; }
      cfg.*(f->ip) = static_cast<int>(*v);
      return true;
    }
    case kFloat: {
      if (value.empty()) { error = L"needs a number: " + key; return false; }
      wchar_t* end = nullptr;
      const float v = std::wcstof(value.c_str(), &end);
      if (*end != L'\0') { error = L"needs a number: " + key; return false; }
      cfg.*(f->fp) = v;
      return true;
    }
    case kStr:
      // play_style/embodiment_fit are strings with a closed vocabulary in
      // config.cpp; reject the typos here so the error names the key.
      if (key == L"play_style" &&
          value != L"standing" && value != L"seated" && value != L"roomscale") {
        error = L"needs standing, seated or roomscale: " + key;
        return false;
      }
      if (key == L"embodiment_fit" && value != L"head" && value != L"arm") {
        error = L"needs head or arm: " + key;
        return false;
      }
      cfg.*(f->sp) = dsvr::Utf8FromWide(value);
      return true;
    case kDominant:
      if (value == L"left") cfg.dominant_hand_left = true;
      else if (value == L"right") cfg.dominant_hand_left = false;
      else { error = L"needs left or right: " + key; return false; }
      return true;
    case kOrientation:
      if (value == L"head")
        cfg.movement_orientation = dsvr::MovementOrientation::Head;
      else if (value == L"controller")
        cfg.movement_orientation = dsvr::MovementOrientation::Controller;
      else { error = L"needs head or controller: " + key; return false; }
      return true;
    case kController:
      if (value == L"non_dominant")
        cfg.movement_controller = dsvr::MovementController::NonDominant;
      else if (value == L"left")
        cfg.movement_controller = dsvr::MovementController::Left;
      else if (value == L"right")
        cfg.movement_controller = dsvr::MovementController::Right;
      else {
        error = L"needs non_dominant, left or right: " + key;
        return false;
      }
      return true;
    default:
      error = L"not editable: " + key;
      return false;
  }
}

std::string LaunchJson(bool ok, unsigned long pid, bool via_steam,
                       const wchar_t* error) {
  std::string out = "{\"ok\":" + std::string(B(ok));
  if (ok) {
    out += ",\"pid\":" + std::to_string(pid);
    out += ",\"via_steam\":" + std::string(B(via_steam));
  } else {
    out += ",\"error\":" + JsonString(error ? error : L"launch_failed");
  }
  return out + "}";
}

std::string StatusJson(const std::wstring& exe, bool game_running,
                       const InstallState& install, const FpsUnlockState& fps,
                       int headset_hz, const char* version) {
  return "{\"exe\":" + JsonString(exe) +
         ",\"game_running\":" + B(game_running) +
         ",\"install_state\":{\"has_loader\":" + B(install.has_loader) +
         ",\"has_core\":" + B(install.has_core) +
         ",\"installed\":" + B(install.installed()) +
         ",\"partial\":" + B(install.partial()) + "}" +
         ",\"fps_unlock_state\":{\"chain_present\":" + B(fps.chain_present) +
         ",\"ini_present\":" + B(fps.ini_present) +
         ",\"proxy_set\":" + B(fps.proxy_set) +
         ",\"installed\":" + B(fps.installed()) +
         ",\"required\":true}" +
         ",\"headset_hz\":" + std::to_string(headset_hz) +
         ",\"version\":" + JsonString(dsvr::WideFromUtf8(version)) + "}";
}

std::string DoctorJson(const std::vector<std::wstring>& checks) {
  bool ok = true;
  for (const auto& line : checks)
    if (line.find(L"[FAIL]") != std::wstring::npos) ok = false;
  std::string out = "{\"checks\":[";
  for (size_t i = 0; i < checks.size(); ++i) {
    if (i) out += ',';
    out += JsonString(checks[i]);
  }
  out += "],\"ok\":";
  out += B(ok);
  out += "}";
  return out;
}

std::string VerifyJson(bool supported, const std::string& sha256) {
  return std::string("{\"ok\":") + B(supported) + ",\"sha256\":\"" + sha256 +
         "\",\"supported\":" + B(supported) + "}";
}

const std::vector<SettingsRow>& SettingsIndex() {
  // Keep in sync with gui.cpp AdvancedSettingsIndex; that copy retires onto
  // this table when the launcher replaces the native settings UI. tests/
  // test_setup_json.cpp pins the row count, key uniqueness and store-facing
  // keys (play_style, theater_override, theater_floor_grid) so a drift here
  // fails offline, not on a headset.
  static const SettingsRow rows[] = {
    {L"Launcher and VR", L"Proxy loaded alongside DSVR", L"chain_proxy"},
    {L"Launcher and VR", L"Start in VR", L"vr_enabled"},
    {L"Launcher and VR", L"Use ReShade host (restart required)", L"reshade_host"},
    {L"Launcher and VR", L"Image resolution scale", L"render_scale_percent"},
    {L"Launcher and VR", L"Keep the desktop image updating", L"desktop_present"},
    {L"Launcher and VR", L"Comfort vignette", L"comfort_vignette"},
    {L"Launcher and VR", L"Vignette strength", L"vignette_strength"},
    {L"Launcher and VR", L"Field-of-view comfort scale", L"fov_scale"},
    {L"Launcher and VR", L"Snap turning", L"snap_turn"},
    {L"Launcher and VR", L"Snap-turn angle", L"snap_turn_degrees"},
    {L"Launcher and VR", L"Smooth turning", L"smooth_turn"},
    {L"Launcher and VR", L"Smooth-turn speed", L"smooth_turn_speed_dps"},
    {L"Launcher and VR", L"Head-tracked view", L"headlook_enabled"},
    {L"Launcher and VR", L"Flat screen during gameplay", L"gameplay_theater"},
    {L"Launcher and VR", L"Mono virtual screen width", L"mono_screen_fov_deg"},
    {L"Launcher and VR", L"Spatial menu panel", L"theater_override"},
    {L"Launcher and VR", L"Floor grid in theater", L"theater_floor_grid"},
    {L"Launcher and VR", L"Menu distance", L"menu_distance_m"},
    {L"Launcher and VR", L"Menu text viewing angle", L"menu_reading_fov_deg"},
    {L"Launcher and VR", L"Disable motion blur while installed", L"force_disable_motion_blur"},
    {L"Launcher and VR", L"Experimental stereo rendering (currently refused)", L"stereo_enabled"},
    {L"Launcher and VR", L"Experimental alternating-eye rendering", L"afr_enabled"},
    {L"Launcher and VR", L"Spatial anti-aliasing (Off/FXAA)", L"aa_mode"},
    {L"Launcher and VR", L"CAS sharpening", L"cas_enabled"},
    {L"Launcher and VR", L"CAS strength", L"cas_strength"},
    {L"Controls and movement", L"Weapon hand", L"dominant_hand"},
    {L"Controls and movement", L"Movement direction reference", L"movement_orientation"},
    {L"Controls and movement", L"Controller used for movement direction", L"movement_controller"},
    {L"Controls and movement", L"Standing, seated, or room-scale play", L"play_style"},
    {L"Controls and movement", L"Arm-swing running", L"arm_swing_sprint"},
    {L"Controls and movement", L"Minimum arm-swing speed", L"arm_swing_stride_min"},
    {L"Controls and movement", L"Maximum arm-swing speed", L"arm_swing_stride_max"},
    {L"Controls and movement", L"Other hand quiet-speed limit", L"arm_swing_quiet_max"},
    {L"Controls and movement", L"Hand alternations before running", L"arm_swing_alternations"},
    {L"Controls and movement", L"Run linger time", L"arm_swing_hold_ms"},
    {L"Controls and movement", L"Reach-to-interact (experimental)", L"reach_interact"},
    {L"Controls and movement", L"Minimum reach distance", L"reach_forward_min_m"},
    {L"Controls and movement", L"Maximum reach distance", L"reach_forward_max_m"},
    {L"Controls and movement", L"Minimum reach speed", L"reach_speed_min"},
    {L"Controls and movement", L"Forward reach-direction requirement", L"reach_dominance_min"},
    {L"Controls and movement", L"Quiet-hand speed limit", L"reach_quiet_max"},
    {L"Controls and movement", L"Hands-still settle time", L"reach_settle_ms"},
    {L"Controls and movement", L"Interaction button hold time", L"reach_press_ms"},
    {L"Controls and movement", L"Reach interaction cooldown", L"reach_cooldown_ms"},
    {L"Controls and movement", L"Keyboard shortcuts", L"hotkeys_enabled"},
    {L"Controls and movement", L"OpenXR controller input", L"xr_input_enabled"},
    {L"Controls and movement", L"Quest 3 controller bindings", L"bind_profile_quest3"},
    {L"Controls and movement", L"Index controller bindings", L"bind_profile_index"},
    {L"Controls and movement", L"Windows Mixed Reality bindings", L"bind_profile_wmr"},
    {L"Controls and movement", L"Basic controller fallback bindings", L"bind_profile_simple"},
    {L"Controls and movement", L"Reconnect controller bindings during a session", L"binding_live_reattach"},
    {L"Controls and movement", L"Log marker shortcut key code", L"log_marker_vk"},
    {L"Controls and movement", L"Reload-settings shortcut key code", L"config_reload_vk"},
    {L"Camera and body", L"Use manual eye height", L"eye_height_override"},
    {L"Camera and body", L"Manual eye height", L"eye_height"},
    {L"Camera and body", L"Seated view-height offset", L"seated_height_offset_m"},
    {L"Camera and body", L"World and avatar scale", L"world_scale"},
    {L"Camera and body", L"Avatar scale trim", L"avatar_scale"},
    {L"Camera and body", L"Camera forward offset", L"forward_offset_m"},
    {L"Camera and body", L"Head clearance", L"eye_clearance_m"},
    {L"Camera and body", L"Hide the full player model", L"hide_player_model"},
    {L"Camera and body", L"Hide head and headgear", L"hide_head"},
    {L"Camera and body", L"Hide torso and legs", L"hide_torso_and_legs"},
    {L"Camera and body", L"Show forearms only", L"forearm_only"},
    {L"Camera and body", L"Hide game interface in VR", L"hide_ui_in_vr"},
    {L"Camera and body", L"Hide the secondary player model", L"hide_second_model"},
    {L"Camera and body", L"Avatar fit anchor", L"embodiment_fit"},
    {L"Combat and haptics", L"Motion-driven attacks", L"motion_attacks"},
    {L"Combat and haptics", L"Legacy swing-speed threshold", L"swing_speed_min"},
    {L"Combat and haptics", L"Legacy swing-damage multiplier", L"swing_damage_gain"},
    {L"Combat and haptics", L"Allow thrust attacks", L"thrust_enabled"},
    {L"Combat and haptics", L"Legacy thrust-speed threshold", L"thrust_speed_min"},
    {L"Combat and haptics", L"Controller vibration", L"haptics"},
    {L"Combat and haptics", L"Follow controllers with avatar arms", L"arm_ik_enabled"},
    {L"Combat and haptics", L"Observe arm tracking without applying it", L"arm_ik_log_only"},
    {L"Combat and haptics", L"Prevent native animation moving tracked body parts", L"arm_ik_block_animation"},
    {L"Combat and haptics", L"Weapon grip roll adjustment", L"grip_roll_correction_deg"},
    {L"Combat and haptics", L"Minimum contact speed", L"contact_speed_min"},
    {L"Combat and haptics", L"Minimum time between hits on one enemy", L"rehit_window_ms"},
    {L"Combat and haptics", L"Physical swing attack input (unqualified)", L"physical_attacks"},
    {L"Combat and haptics", L"Light-swing speed threshold", L"swing_attack_speed_min"},
    {L"Combat and haptics", L"Strong-swing speed threshold", L"swing_attack_heavy_min"},
    {L"Combat and haptics", L"Physical shield defense input (unqualified)", L"physical_defence"},
    {L"Combat and haptics", L"Shield-hand settle time", L"guard_settle_ms"},
    {L"Combat and haptics", L"Shield flick speed threshold", L"guard_flick_speed_min"},
    {L"Combat and haptics", L"Parry button hold time", L"guard_parry_press_ms"},
    {L"Combat and haptics", L"Parry cooldown", L"guard_parry_cooldown_ms"},
    {L"Combat and haptics", L"Offline combat test bench (no native damage)", L"combat_test_bench"},
    {L"Combat and haptics", L"Test target distance", L"combat_test_target_m"},
    {L"Combat and haptics", L"Minimum separation between hands", L"weapon_separation_min_m"},
    {L"Combat and haptics", L"Weapon mass table file", L"weapon_mass_table_path"},
    {L"Combat and haptics", L"Weapon torque response", L"torque_response"},
    {L"Combat and haptics", L"Support-hand half-sword reach", L"halfsword_envelope_m"},
    {L"Combat and haptics", L"Deflection effect strength", L"deflect_amplitude"},
    {L"Combat and haptics", L"Deflection effect duration", L"deflect_duration_ms"},
    {L"Diagnostics and safety", L"Detailed diagnostic logging", L"verbose_logging"},
    {L"Diagnostics and safety", L"Desktop observer mode (startup only)", L"observer_only"},
    {L"Diagnostics and safety", L"Combat trace mode (startup only)", L"combat_trace_only"},
    {L"Diagnostics and safety", L"Autonomous VR session telemetry (startup only)", L"autonomous_session_capture"},
    {L"Diagnostics and safety", L"Show developer controls in Setup", L"dev_mode"},
    {L"Diagnostics and safety", L"Confirm offline-only use", L"offline_only_ack"},
    {L"Diagnostics and safety", L"Original motion-blur value (Setup-managed)", L"motion_blur_original"},
  };
  static const std::vector<SettingsRow> table(rows, rows + sizeof(rows) / sizeof(rows[0]));
  return table;
}

std::string SettingsIndexJson() {
  std::string out = "{\"rows\":[";
  const auto& rows = SettingsIndex();
  for (size_t i = 0; i < rows.size(); ++i) {
    if (i) out += ',';
    out += "{\"group\":" + JsonString(rows[i].group) +
           ",\"label\":" + JsonString(rows[i].label) +
           ",\"key\":" + JsonString(rows[i].key) + "}";
  }
  out += "]}";
  return out;
}

std::string PresetIndexJson() {
  std::string out = "{\"presets\":[";
  for (size_t i = 0; i < dsvr::kUserPresetCount; ++i) {
    const dsvr::UserPresetInfo& preset = dsvr::kUserPresets[i];
    if (i) out += ',';
    out += "{\"id\":" + JsonString(dsvr::WideFromUtf8(preset.verb)) +
           ",\"label\":" + JsonString(dsvr::WideFromUtf8(preset.label)) +
           ",\"detail\":" + JsonString(dsvr::WideFromUtf8(preset.detail)) +
           ",\"hardware_fit\":" + (preset.hardware_fit ? "true" : "false") + "}";
  }
  out += "]}";
  return out;
}

}  // namespace setup
