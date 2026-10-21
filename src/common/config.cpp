#include "config.h"
#include "logging.h"

#include <windows.h>

#include <charconv>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

namespace dsvr {
namespace {

std::string ToLower(std::string s) {
  for (auto& c : s) c = (char)tolower((unsigned char)c);
  return s;
}

std::string Trim(std::string_view v) {
  while (!v.empty() && (v.front() == ' ' || v.front() == '\t' ||
                        v.front() == '\r' || v.front() == '\n'))
    v.remove_prefix(1);
  while (!v.empty() && (v.back() == ' ' || v.back() == '\t' ||
                        v.back() == '\r' || v.back() == '\n'))
    v.remove_suffix(1);
  return std::string(v);
}

bool ParseBool(std::string_view v, bool& out) {
  std::string t = ToLower(Trim(v));
  if (t == "true" || t == "1" || t == "yes" || t == "on") {
    out = true;
    return true;
  }
  if (t == "false" || t == "0" || t == "no" || t == "off") {
    out = false;
    return true;
  }
  return false;
}

bool ParseInt(std::string_view v, int& out) {
  std::string t = Trim(v);
  // Accept 0x-prefixed hex (virtual-key codes read better as 0x78).
  int base = 10;
  const char* beg = t.data();
  const char* end = beg + t.size();
  if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) {
    base = 16;
    beg += 2;
  }
  if (beg >= end) return false;
  auto r = std::from_chars(beg, end, out, base);
  return r.ec == std::errc() && r.ptr == end;
}

bool ParseFloat(std::string_view v, float& out) {
  std::string t = Trim(v);
  char* e = nullptr;
  out = strtof(t.c_str(), &e);
  return e && e != t.c_str() && *e == '\0' && std::isfinite(out);
}

template <typename T>
T Clamp(T v, T lo, T hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

void Apply(Config& c, std::string_view section_raw, std::string_view key_raw,
           std::string_view value) {
  std::string s = ToLower(Trim(section_raw));
  std::string k = ToLower(Trim(key_raw));
  if (s == "loader") {
    if (k == "chain_proxy" || k == "chain")
      c.loader_chain_proxy = Trim(value);
  } else if (s == "vr") {
    bool b;
    int i;
    float f;
    if (k == "vr_enabled" && ParseBool(value, b)) c.vr_enabled = b;
    if (k == "reshade_host" && ParseBool(value, b)) c.reshade_host = b;
    if (k == "frame_limit_fps" && ParseInt(value, i) &&
        (i == 60 || i == 72 || i == 90 || i == 120 || i == 144 || i == 240))
      c.frame_limit_fps = i;
    if (k == "render_scale_percent" && ParseInt(value, i))
      c.render_scale_percent = Clamp(i, 50, 200);
    if (k == "desktop_present" && ParseBool(value, b)) c.desktop_present = b;
    if (k == "comfort_vignette" && ParseBool(value, b)) c.comfort_vignette = b;
    if (k == "vignette_strength" && ParseFloat(value, f))
      c.vignette_strength = Clamp(f, 0.0f, 1.0f);
      if ((k == "fov_scale" || k == "fov_comfort_scale") && ParseFloat(value, f))
      c.fov_scale = Clamp(f, 0.7f, 1.3f);
    if (k == "snap_turn" && ParseBool(value, b)) c.snap_turn = b;
    if (k == "snap_turn_degrees" && ParseInt(value, i))
      c.snap_turn_degrees = Clamp(i, 15, 90);
    if (k == "smooth_turn" && ParseBool(value, b)) c.smooth_turn = b;
    if ((k == "smooth_turn_speed_dps" || k == "smooth_turn_speed") &&
        ParseFloat(value, f))
      c.smooth_turn_speed_dps = Clamp(f, 30.0f, 360.0f);
    if (k == "head_directed_move" && ParseBool(value, b))
      c.head_directed_move = b;

    if (k == "headlook_enabled" && ParseBool(value, b))
      c.headlook_enabled = b;
    if (k == "mono_screen_fov_deg" && ParseFloat(value, f))
      c.mono_screen_fov_deg = Clamp(f, 30.0f, 110.0f);
    if (k == "sanctuary_override" && ParseBool(value, b))
      c.sanctuary_override = b;
    if (k == "menu_distance_m" && ParseFloat(value, f) && std::isfinite(f))
      c.menu_distance_m = Clamp(f, 1.0f, 4.0f);
    if (k == "menu_reading_fov_deg" && ParseFloat(value, f) && std::isfinite(f))
      c.menu_reading_fov_deg = Clamp(f, 45.0f, 80.0f);
    if (k == "force_disable_motion_blur" && ParseBool(value, b))
      c.force_disable_motion_blur = b;
    if (k == "stereo_enabled" && ParseBool(value, b)) c.stereo_enabled = b;
    if (k == "afr_enabled" && ParseBool(value, b)) c.afr_enabled = b;
  } else if (s == "controls") {
    bool b;
    int i;
    if (k == "movement_orientation") {
      const auto v = ToLower(Trim(value));
      if (v == "head") c.movement_orientation = MovementOrientation::Head;
      else if (v == "controller") c.movement_orientation = MovementOrientation::Controller;
    }
    if ((k == "dominant_hand_left" || k == "left_handed") &&
        ParseBool(value, b))
      c.dominant_hand_left = b;
    if (k == "dominant_hand") {
      std::string v = ToLower(Trim(value));
      if (v == "left" || v == "l") c.dominant_hand_left = true;
      if (v == "right" || v == "r") c.dominant_hand_left = false;
    }
    if (k == "movement_reference") {
      std::string v = ToLower(Trim(value));
      if (v == "head") c.movement_reference = MovementReference::Head;
      if (v == "left_hand" || v == "left-hand")
        c.movement_reference = MovementReference::LeftHand;
      if (v == "right_hand" || v == "right-hand")
        c.movement_reference = MovementReference::RightHand;
    }
    if (k == "movement_controller") {
      const auto v = ToLower(Trim(value));
      if (v == "non_dominant") c.movement_controller = MovementController::NonDominant;
      else if (v == "left") c.movement_controller = MovementController::Left;
      else if (v == "right") c.movement_controller = MovementController::Right;
    }
    if (k == "play_style") {
      std::string v = ToLower(Trim(value));
      if (v == "standing" || v == "seated" || v == "roomscale")
        c.play_style = v;
    }
    if (k == "hotkeys_enabled" && ParseBool(value, b)) c.hotkeys_enabled = b;
    if ((k == "xr_input_enabled" || k == "xr_actions_enabled") &&
        ParseBool(value, b))
      c.xr_input_enabled = b;
    if (k == "log_marker_vk" && ParseInt(value, i))
      c.log_marker_vk = Clamp(i, 0, 255);
    if (k == "config_reload_vk" && ParseInt(value, i))
      c.config_reload_vk = Clamp(i, 0, 255);
  } else if (s == "camera") {
    bool b;
    float f;
    if (k == "eye_height" && ParseFloat(value, f))
      c.eye_height = Clamp(f, 0.5f, 2.5f);
    if (k == "world_scale" && ParseFloat(value, f))
      c.world_scale = Clamp(f, 0.5f, 2.0f);
    if (k == "forward_offset_m" && ParseFloat(value, f))
      c.forward_offset_m = Clamp(f, 0.0f, 0.4f);
    if (k == "hide_head" && ParseBool(value, b)) c.hide_head = b;
    if (k == "forearm_only" && ParseBool(value, b)) c.forearm_only = b;
    if (k == "hide_ui_in_vr" && ParseBool(value, b)) c.hide_ui_in_vr = b;
    if (k == "hide_player_model" && ParseBool(value, b))
      c.hide_player_model = b;
    if ((k == "hide_second_model" || k == "hide_weapon_model") &&
        ParseBool(value, b))
      c.hide_second_model = b;
  } else if (s == "combat") {
    bool b;
    float f;
    int i;
    if (k == "motion_attacks" && ParseBool(value, b)) c.motion_attacks = b;
    if (k == "swing_speed_min" && ParseFloat(value, f))
      c.swing_speed_min = Clamp(f, 0.2f, 10.0f);
    if (k == "swing_damage_gain" && ParseFloat(value, f))
      c.swing_damage_gain = Clamp(f, 0.1f, 5.0f);
    if (k == "thrust_enabled" && ParseBool(value, b)) c.thrust_enabled = b;
    if (k == "thrust_speed_min" && ParseFloat(value, f))
      c.thrust_speed_min = Clamp(f, 0.2f, 10.0f);
    if (k == "haptics" && ParseBool(value, b)) c.haptics = b;
    if (k == "arm_ik_enabled" && ParseBool(value, b)) c.arm_ik_enabled = b;
    if (k == "arm_ik_log_only" && ParseBool(value, b)) c.arm_ik_log_only = b;
    if (k == "grip_roll_correction_deg" && ParseFloat(value, f))
      c.grip_roll_correction_deg = Clamp(f, -45.0f, 45.0f);
    if (k == "contact_speed_min" && ParseFloat(value, f))
      c.contact_speed_min = Clamp(f, 0.1f, 5.0f);
    if (k == "rehit_window_ms" && ParseInt(value, i))
      c.rehit_window_ms = Clamp(i, 50, 2000);
      if (k == "combat_test_bench" && ParseBool(value, b)) c.combat_test_bench = b;
      if (k == "combat_test_target_m" && ParseFloat(value, f))
      c.combat_test_target_m = Clamp(f, 0.5f, 2.0f);
      if (k == "weapon_separation_min_m" && ParseFloat(value, f))
      c.weapon_separation_min_m = Clamp(f, 0.0f, 0.5f);
  } else if (s == "diagnostics") {
    bool b;
    if (k == "verbose_logging" && ParseBool(value, b)) c.verbose_logging = b;
    if (k == "observer_only" && ParseBool(value, b)) c.observer_only = b;
    if (k == "hotkeys_enabled" && ParseBool(value, b)) c.hotkeys_enabled = b;
    if (k == "log_marker_vk") {
      int i;
      if (ParseInt(value, i)) c.log_marker_vk = Clamp(i, 0, 255);
    }
  } else if (s == "safety") {
    bool b;
    if (k == "offline_only_ack" && ParseBool(value, b)) c.offline_only_ack = b;
    if (k == "motion_blur_original") {
      std::string v = Trim(value);
      bool digits = !v.empty() && v.size() <= 4;
      for (char ch : v) digits = digits && ch >= '0' && ch <= '9';
      if (digits) c.motion_blur_original = v;
    }
  }
}

std::string FmtFloat(float v) {
  char buf[64]{};
  // Shortest round-trip-ish: trim trailing zeros for readability.
  snprintf(buf, sizeof(buf), "%.4g", (double)v);
  return buf;
}

}  // namespace

std::wstring WideFromUtf8(const std::string& utf8) {
  if (utf8.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(),
                              nullptr, 0);
  std::wstring w((size_t)(n > 0 ? n : 0), L'\0');
  if (n > 0)
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), w.data(),
                        n);
  return w;
}

std::string Utf8FromWide(const std::wstring& wide) {
  if (wide.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(),
                              nullptr, 0, nullptr, nullptr);
  std::string s((size_t)(n > 0 ? n : 0), '\0');
  if (n > 0)
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(), s.data(),
                        n, nullptr, nullptr);
  return s;
}

std::wstring GameExeDirectory() {
  wchar_t dir[32768]{};
  DWORD n = GetModuleFileNameW(nullptr, dir, 32767);
  if (n == 0 || n >= 32767) return L".";
  std::wstring p(dir, n);
  auto pos = p.find_last_of(L"\\/");
  if (pos == std::wstring::npos) return L".";
  return p.substr(0, pos);
}

std::wstring ConfigPathNextToExe() {
  std::wstring dir = GameExeDirectory();
  if (!dir.empty() && dir.back() != L'\\' && dir.back() != L'/') dir += L"\\";
  return dir + L"DSVR.toml";
}

bool ConfigLoad(const wchar_t* toml_path, Config& out) {
  out = Config{};
  FILE* f = nullptr;
  if (_wfopen_s(&f, toml_path, L"r") != 0 || !f) return false;
  char line[1024];
  std::string section;
  bool orientation_seen = false;
  bool controller_seen = false;
  bool invalid_movement_setting = false;
  bool legacy_axis_bypass_seen = false;
  while (fgets(line, sizeof(line), f)) {
    std::string_view v(line);
    if (auto p = v.find('#'); p != std::string_view::npos)
      v = v.substr(0, p);
    std::string t = Trim(v);
    if (t.empty()) continue;
    if (t.front() == '[' && t.back() == ']') {
      section = Trim(std::string_view(t.data() + 1, t.size() - 2));
      continue;
    }
    auto eq = t.find('=');
    if (eq == std::string::npos) continue;
    std::string key = Trim(std::string_view(t.data(), eq));
    std::string val = Trim(std::string_view(t.data() + eq + 1));
    if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
      val = val.substr(1, val.size() - 2);
    const auto normalized_section = ToLower(Trim(section));
    const auto normalized_key = ToLower(Trim(key));
    if (normalized_section == "vr" && normalized_key == "head_directed_move") {
      bool legacy_enabled = true;
      if (ParseBool(val, legacy_enabled) && !legacy_enabled)
        legacy_axis_bypass_seen = true;
    }
    if (normalized_section == "controls" && normalized_key == "movement_orientation") {
      orientation_seen = true;
      const auto mode = ToLower(Trim(val));
      invalid_movement_setting |= mode != "head" && mode != "controller";
    }
    if (normalized_section == "controls" && normalized_key == "movement_controller") {
      controller_seen = true;
      const auto hand = ToLower(Trim(val));
      invalid_movement_setting |= hand != "non_dominant" && hand != "left" && hand != "right";
    }
    Apply(out, section, key, val);
  }
  fclose(f);
  if (legacy_axis_bypass_seen)
    DSVR_WARN("Config: head_directed_move=false is retired; movement now uses the selected Head/Controller orientation");
  if (invalid_movement_setting) return false;
  if (!orientation_seen) {
    out.movement_orientation = out.movement_reference == MovementReference::Head
        ? MovementOrientation::Head : MovementOrientation::Controller;
  }
  if (!controller_seen) {
    out.movement_controller = out.movement_reference == MovementReference::LeftHand
        ? MovementController::Left
        : out.movement_reference == MovementReference::RightHand
            ? MovementController::Right : MovementController::NonDominant;
  }
  return true;
}

bool ConfigSave(const wchar_t* toml_path, const Config& c) {
  if (!ConfigValid(c)) return false;
  FILE* f = nullptr;
  if (_wfopen_s(&f, toml_path, L"w") != 0 || !f) return false;
  fprintf(f,
      "# DSVR settings -- edit here or with DSVR-Setup (GUI). Delete to reset.\n"
      "# WARNING: offline play ONLY while DSVR is installed.\n"
      "# Remove via DSVR-Setup --uninstall before any online play.\n\n");
  fprintf(f, "[loader]\n");
  fprintf(f,
      "# Chain-load another dinput8-style proxy (filename in game folder).\n"
      "# Example: chain_proxy = \"dinput8-chain.dll\"  (empty = none)\n");
  fprintf(f, "chain_proxy = \"%s\"\n\n", c.loader_chain_proxy.c_str());

  fprintf(f, "[vr]\n");
  fprintf(f, "vr_enabled = %s\n", c.vr_enabled ? "true" : "false");
  fprintf(f, "render_scale_percent = %d\n", c.render_scale_percent);
  fprintf(f, "# Requested game/XR target; default 90. Native limiter application and runtime cadence are separate.\n");
  fprintf(f, "frame_limit_fps = %d\n", c.frame_limit_fps);
  fprintf(f, "# false skips the desktop swapchain Present (XR still copies the backbuffer).\n");
  fprintf(f, "desktop_present = %s\n", c.desktop_present ? "true" : "false");
  fprintf(f, "comfort_vignette = %s\n", c.comfort_vignette ? "true" : "false");
  fprintf(f, "vignette_strength = %s\n", FmtFloat(c.vignette_strength).c_str());
  fprintf(f, "# In-game FOV comfort scaler (1.0 = headset truth; lower = narrower, calmer).\n");
  fprintf(f, "fov_scale = %s\n", FmtFloat(c.fov_scale).c_str());
  fprintf(f, "snap_turn = %s\n", c.snap_turn ? "true" : "false");
  fprintf(f, "# Startup-only: requires the DSVR ReShade add-on; no hot fallback.\n");
  fprintf(f, "reshade_host = %s\n", c.reshade_host ? "true" : "false");
  fprintf(f, "snap_turn_degrees = %d\n", c.snap_turn_degrees);
  fprintf(f, "smooth_turn = %s\n", c.smooth_turn ? "true" : "false");
  fprintf(f, "smooth_turn_speed_dps = %s\n",
          FmtFloat(c.smooth_turn_speed_dps).c_str());
  fprintf(f, "# headlook_enabled = true makes HMD rotation drive the camera (P3 test)\n");
  fprintf(f, "headlook_enabled = %s\n", c.headlook_enabled ? "true" : "false");
  fprintf(f, "# mono_screen_fov_deg: virtual cinema screen width in mono mode\n");
  fprintf(f, "mono_screen_fov_deg = %s\n\n",
          FmtFloat(c.mono_screen_fov_deg).c_str());
  fprintf(f, "# Spatial menu canvas: F8 toggles override, F6 recentres.\n");
  fprintf(f, "sanctuary_override = %s\n", c.sanctuary_override ? "true" : "false");
  fprintf(f, "menu_distance_m = %s\n", FmtFloat(c.menu_distance_m).c_str());
  fprintf(f, "menu_reading_fov_deg = %s\n", FmtFloat(c.menu_reading_fov_deg).c_str());
  fprintf(f, "# Setup enforces the game's Blur=0 while installed (restored on uninstall).\n");
  fprintf(f, "force_disable_motion_blur = %s\n", c.force_disable_motion_blur ? "true" : "false");
  fprintf(f, "# Experimental: no native eye renderer bound yet; stereo logs a refusal and stays mono.\n");
  fprintf(f, "stereo_enabled = %s\n", c.stereo_enabled ? "true" : "false");
  fprintf(f, "# Experimental: alternating eyes; refused unless stereo is active.\n");
  fprintf(f, "afr_enabled = %s\n\n", c.afr_enabled ? "true" : "false");

  fprintf(f, "[controls]\n");
  fprintf(f, "dominant_hand = \"%s\"\n",
          c.dominant_hand_left ? "left" : "right");
  fprintf(f, "movement_orientation = \"%s\"\n",
          c.movement_orientation == MovementOrientation::Controller ? "controller" : "head");
  const char* movement_controller =
      c.movement_controller == MovementController::Left ? "left" :
      c.movement_controller == MovementController::Right ? "right" : "non_dominant";
  fprintf(f, "movement_controller = \"%s\"\n", movement_controller);
  fprintf(f, "play_style = \"%s\"\n", c.play_style.c_str());
  fprintf(f, "hotkeys_enabled = %s\n", c.hotkeys_enabled ? "true" : "false");
  fprintf(f, "# xr_input_enabled = true turns on OpenXR controller actions + pad bridge\n");
  fprintf(f, "xr_input_enabled = %s\n", c.xr_input_enabled ? "true" : "false");
  fprintf(f, "log_marker_vk = 0x%02x\n", (unsigned)c.log_marker_vk);
  fprintf(f, "config_reload_vk = 0x%02x\n\n", (unsigned)c.config_reload_vk);

  fprintf(f, "[camera]\n");
  fprintf(f, "eye_height = %s\n", FmtFloat(c.eye_height).c_str());
  fprintf(f, "world_scale = %s\n", FmtFloat(c.world_scale).c_str());
  fprintf(f, "# Horizontal clearance from the player mesh; 0 disables it.\n");
  fprintf(f, "forward_offset_m = %s\n", FmtFloat(c.forward_offset_m).c_str());
  fprintf(f, "hide_player_model = %s\n", c.hide_player_model ? "true" : "false");
  fprintf(f, "hide_head = %s\n", c.hide_head ? "true" : "false");
  fprintf(f, "forearm_only = %s\n", c.forearm_only ? "true" : "false");
  fprintf(f, "hide_ui_in_vr = %s\n", c.hide_ui_in_vr ? "true" : "false");
  fprintf(f, "# Second player render model (weapon/shield side) stays visible\n");
  fprintf(f, "# unless this is true. Flip it if your body shows but weapon does not.\n");
  fprintf(f, "hide_second_model = %s\n\n", c.hide_second_model ? "true" : "false");

  fprintf(f, "[combat]\n");
  fprintf(f, "motion_attacks = %s\n", c.motion_attacks ? "true" : "false");
  fprintf(f, "swing_speed_min = %s\n", FmtFloat(c.swing_speed_min).c_str());
  fprintf(f, "swing_damage_gain = %s\n",
          FmtFloat(c.swing_damage_gain).c_str());
  fprintf(f, "thrust_enabled = %s\n", c.thrust_enabled ? "true" : "false");
  fprintf(f, "thrust_speed_min = %s\n", FmtFloat(c.thrust_speed_min).c_str());
  fprintf(f, "haptics = %s\n", c.haptics ? "true" : "false");
  fprintf(f, "# IK arms: model arms follow controllers; kills canned melee.\n");
  fprintf(f, "arm_ik_enabled = %s\n", c.arm_ik_enabled ? "true" : "false");
  fprintf(f, "arm_ik_log_only = %s\n", c.arm_ik_log_only ? "true" : "false");
  fprintf(f, "grip_roll_correction_deg = %s\n",
          FmtFloat(c.grip_roll_correction_deg).c_str());
  fprintf(f, "contact_speed_min = %s\n", FmtFloat(c.contact_speed_min).c_str());
  fprintf(f, "rehit_window_ms = %d\n", c.rehit_window_ms);
  fprintf(f, "# Proving ground: fixed sparring target + live blade state, never native damage.\n");
  fprintf(f, "combat_test_bench = %s\n", c.combat_test_bench ? "true" : "false");
  fprintf(f, "combat_test_target_m = %s\n", FmtFloat(c.combat_test_target_m).c_str());
  fprintf(f, "# Min kept separation between solved hands; 0 = off.\n");
  fprintf(f, "weapon_separation_min_m = %s\n\n", FmtFloat(c.weapon_separation_min_m).c_str());

  fprintf(f, "[diagnostics]\n");
  fprintf(f, "verbose_logging = %s\n", c.verbose_logging ? "true" : "false");
  fprintf(f, "# Startup-only desktop observer: no XR, gameplay writes or injected input.\n");
  fprintf(f, "# Does NOT isolate saves or enforce offline play. Restart to change.\n");
  fprintf(f, "observer_only = %s\n\n", c.observer_only ? "true" : "false");

  fprintf(f, "[safety]\n");
  fprintf(f, "offline_only_ack = %s\n", c.offline_only_ack ? "true" : "false");
  fprintf(f, "# Setup-owned: game Blur literal recorded before forcing (empty = none).\n");
  fprintf(f, "motion_blur_original = \"%s\"\n", c.motion_blur_original.c_str());
  const bool written = ferror(f) == 0;
  return fclose(f) == 0 && written;
}

bool ConfigValid(const Config& c) {
  auto range = [](float v, float lo, float hi) {
    return std::isfinite(v) && v >= lo && v <= hi;
  };
  return c.render_scale_percent >= 50 && c.render_scale_percent <= 200 &&
      (c.frame_limit_fps == 60 || c.frame_limit_fps == 72 || c.frame_limit_fps == 90 ||
       c.frame_limit_fps == 120 || c.frame_limit_fps == 144 || c.frame_limit_fps == 240) &&
      range(c.vignette_strength, 0, 1) && range(c.fov_scale, 0.7f, 1.3f) &&
      c.snap_turn_degrees >= 15 && c.snap_turn_degrees <= 90 &&
      range(c.smooth_turn_speed_dps, 30, 360) && range(c.mono_screen_fov_deg, 30, 110) &&
      range(c.menu_distance_m, 1, 4) && range(c.menu_reading_fov_deg, 45, 80) &&
      range(c.eye_height, .5f, 2.5f) && range(c.world_scale, .5f, 2) &&
      range(c.forward_offset_m, 0, .4f) && range(c.swing_speed_min, .2f, 10) &&
      range(c.swing_damage_gain, .1f, 5) && range(c.thrust_speed_min, .2f, 10) &&
      range(c.grip_roll_correction_deg, -45, 45) && range(c.contact_speed_min, .1f, 5) &&
      c.rehit_window_ms >= 50 && c.rehit_window_ms <= 2000 &&
      range(c.combat_test_target_m, 0.5f, 2.0f) && range(c.weapon_separation_min_m, 0.0f, 0.5f) &&
      c.log_marker_vk >= 0 && c.log_marker_vk <= 255 && c.config_reload_vk >= 0 && c.config_reload_vk <= 255 &&
      (c.play_style == "standing" || c.play_style == "seated" || c.play_style == "roomscale") &&
      (c.movement_reference == MovementReference::Head ||
       c.movement_reference == MovementReference::LeftHand ||
       c.movement_reference == MovementReference::RightHand) &&
      (c.movement_orientation == MovementOrientation::Head ||
       c.movement_orientation == MovementOrientation::Controller) &&
      (c.movement_controller == MovementController::NonDominant ||
       c.movement_controller == MovementController::Left ||
       c.movement_controller == MovementController::Right) &&
      c.loader_chain_proxy.find_first_of("\r\n\"") == std::string::npos &&
      (c.motion_blur_original.empty() ||
       (c.motion_blur_original.size() <= 4 &&
        c.motion_blur_original.find_first_not_of("0123456789") == std::string::npos));
}

bool ConfigSaveDefaults(const wchar_t* toml_path) {
  return ConfigSave(toml_path, Config{});
}

}  // namespace dsvr
