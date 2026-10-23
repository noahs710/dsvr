// Config round-trip test: defaults save, hand-edited values load, out-of-range
// values clamp, unknown keys/sections are ignored. No game needed.
//
// Uses CHECK (always executes) instead of assert so Release builds with NDEBUG
// still run every call.
#include <windows.h>

#include <cstdio>
#include <string>

#include "../src/common/config.h"

#define CHECK(cond)                                                      \
  do {                                                                   \
    if (!(cond)) {                                                       \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
      return 1;                                                          \
    }                                                                    \
  } while (0)

int main() {
  wchar_t tmp[32768]{};
  GetTempPathW(32767, tmp);
  std::wstring path = std::wstring(tmp) + L"DSVR-test-config.toml";
  DeleteFileW(path.c_str());

  // Save defaults, then load them back.
  CHECK(dsvr::ConfigSaveDefaults(path.c_str()));
  dsvr::Config c;
  CHECK(dsvr::ConfigLoad(path.c_str(), c));
  CHECK(!c.vr_enabled);
  CHECK(!c.reshade_host);
  CHECK(!c.observer_only);
  CHECK(c.render_scale_percent == 100);
  CHECK(c.snap_turn_degrees == 45);
  CHECK(c.movement_orientation == dsvr::MovementOrientation::Head);
  CHECK(c.movement_controller == dsvr::MovementController::NonDominant);
  CHECK(c.eye_height == 1.65f && !c.eye_height_override);
  CHECK(c.forward_offset_m == 0.0f && !c.hide_player_model);
  CHECK(c.eye_clearance_m == 0.45f);
  CHECK(c.hide_head);
  CHECK(c.embodiment_fit == "head");
  CHECK(c.motion_attacks);
  CHECK(!c.offline_only_ack);
  CHECK(!c.sanctuary_override && c.menu_distance_m == 2.75f);
  CHECK(c.menu_reading_fov_deg == 52.0f);
  CHECK(c.frame_limit_fps == 90);
  CHECK(c.grip_roll_correction_deg == 12.0f);
  CHECK(!c.force_disable_motion_blur && c.motion_blur_original.empty());
  CHECK(!c.stereo_enabled);
  CHECK(!c.afr_enabled);

  // Hand-edited values.
  {
    FILE* f = nullptr;
    CHECK(_wfopen_s(&f, path.c_str(), L"w") == 0 && f);
    fputs("[vr]\nvr_enabled = true\nreshade_host = true\nrender_scale_percent = 150\n"
          "frame_limit_fps = 144\n"
          "snap_turn_degrees = 30\nhead_directed_move = false\n"
          "[camera]\neye_height = 1.8\nworld_scale = 1.2\n"
          "eye_clearance_m = 0.6\n"
          "embodiment_fit = \"arm\"\n"
          "forward_offset_m = 0.12\nhide_player_model = false\nhide_head = false\n"
          "[combat]\nswing_speed_min = 2.5\nswing_damage_gain = 1.5\n"
          "haptics = false\n[safety]\noffline_only_ack = true\n"
          "[unknown_section]\nbogus = 1\n",
          f);
    fclose(f);
  }
  CHECK(dsvr::ConfigLoad(path.c_str(), c));
  CHECK(c.vr_enabled);
  CHECK(c.reshade_host);
  CHECK(c.render_scale_percent == 150);
  CHECK(c.frame_limit_fps == 144);
  CHECK(c.snap_turn_degrees == 30);
  CHECK(c.movement_orientation == dsvr::MovementOrientation::Head);
  CHECK(!c.head_directed_move);
  CHECK(c.eye_height == 1.8f && !c.eye_height_override); // legacy value is not opt-in
  c.eye_height_override=true;
  CHECK(dsvr::ConfigSave(path.c_str(),c));
  CHECK(dsvr::ConfigLoad(path.c_str(),c) && c.eye_height_override && c.eye_height==1.8f);
  CHECK(c.world_scale == 1.2f);
  CHECK(c.forward_offset_m == 0.12f && !c.hide_player_model);
  CHECK(c.eye_clearance_m == 0.6f);
  CHECK(!c.hide_head);
  CHECK(c.embodiment_fit == "arm");
  CHECK(c.swing_damage_gain == 1.5f);
  CHECK(!c.haptics);
  CHECK(c.offline_only_ack);

  // Clamping.
  {
    FILE* f = nullptr;
    CHECK(_wfopen_s(&f, path.c_str(), L"w") == 0 && f);
    fputs("[vr]\nrender_scale_percent = 999\nsnap_turn_degrees = 1\n"
          "[camera]\neye_height = 99.0\nforward_offset_m = 99.0\n"
          "eye_clearance_m = 99.0\n"
          "[combat]\nswing_damage_gain = 0.0\n",
          f);
    fclose(f);
  }
  CHECK(dsvr::ConfigLoad(path.c_str(), c));
  CHECK(c.render_scale_percent == 200);
  CHECK(!c.reshade_host);  // old configurations retain the legacy host
  CHECK(c.snap_turn_degrees == 15);
  CHECK(c.eye_height == 2.5f);
  CHECK(c.forward_offset_m == 0.4f);
  CHECK(c.eye_clearance_m == 1.2f);
  CHECK(c.swing_damage_gain == 0.1f);

  // Missing file keeps defaults and reports false.
  DeleteFileW(path.c_str());
  CHECK(!dsvr::ConfigLoad(path.c_str(), c));
  CHECK(!c.vr_enabled && c.render_scale_percent == 100);

  // QoL batch 1 + GUI fields: chain, hand, style, hotkey hex, round-trip.
  {
    FILE* f = nullptr;
    CHECK(_wfopen_s(&f, path.c_str(), L"w") == 0 && f);
    fputs("[loader]\nchain_proxy = \"dinput8-chain.dll\"\n"
          "[vr]\nvignette_strength = 0.5\nsmooth_turn_speed_dps = 180\nfov_scale = 0.85\n"
          "[controls]\ndominant_hand = \"left\"\nplay_style = \"seated\"\n"
          "log_marker_vk = 0x79\nconfig_reload_vk = 119\n"
          "[diagnostics]\nverbose_logging = true\nobserver_only = true\n",
          f);
    fclose(f);
  }
  CHECK(dsvr::ConfigLoad(path.c_str(), c));
  CHECK(c.loader_chain_proxy == "dinput8-chain.dll");
  CHECK(c.dominant_hand_left);
  CHECK(c.play_style == "seated");
  CHECK(c.vignette_strength == 0.5f);
  CHECK(c.smooth_turn_speed_dps == 180.0f);
  CHECK(c.fov_scale == 0.85f);
  CHECK(c.log_marker_vk == 0x79);
  CHECK(c.config_reload_vk == 119);
  CHECK(c.verbose_logging);
  CHECK(c.observer_only);
  // Invalid play_style is rejected (keeps previous/default).
  {
    FILE* f = nullptr;
    CHECK(_wfopen_s(&f, path.c_str(), L"w") == 0 && f);
    fputs("[controls]\nplay_style = \"flying\"\n", f);
    fclose(f);
  }
  CHECK(dsvr::ConfigLoad(path.c_str(), c));
  CHECK(c.play_style == "standing");

  // An invalid embodiment_fit keeps the documented default, the same way an
  // unknown play_style does: a typo must not silently disable the fit.
  {
    FILE* f = nullptr;
    CHECK(_wfopen_s(&f, path.c_str(), L"w") == 0 && f);
    fputs("[camera]\nembodiment_fit = \"knees\"\n", f);
    fclose(f);
  }
  CHECK(dsvr::ConfigLoad(path.c_str(), c));
  CHECK(c.embodiment_fit == "head");

  // Legacy movement_reference values migrate unless canonical keys are present.
  {
    FILE* f = nullptr;
    CHECK(_wfopen_s(&f, path.c_str(), L"w") == 0 && f);
    fputs("[controls]\nmovement_reference = \"left_hand\"\n", f);
    fclose(f);
    CHECK(dsvr::ConfigLoad(path.c_str(), c));
    CHECK(c.movement_orientation == dsvr::MovementOrientation::Controller);
    CHECK(c.movement_controller == dsvr::MovementController::Left);
    CHECK(_wfopen_s(&f, path.c_str(), L"w") == 0 && f);
    fputs("[controls]\nmovement_orientation = \"controller\"\nmovement_controller = \"right\"\nmovement_reference = \"head\"\n", f);
    fclose(f);
    CHECK(dsvr::ConfigLoad(path.c_str(), c));
    CHECK(c.movement_orientation == dsvr::MovementOrientation::Controller);
    CHECK(c.movement_controller == dsvr::MovementController::Right);
    CHECK(_wfopen_s(&f, path.c_str(), L"w") == 0 && f);
    fputs("[controls]\nmovement_orientation = \"invalid\"\n", f);
    fclose(f);
    CHECK(!dsvr::ConfigLoad(path.c_str(), c));
  }

  // ConfigSave -> ConfigLoad round-trip (GUI persists through this path).
  {
    dsvr::Config want;
    want.vr_enabled = true;
    want.reshade_host = true;
    want.observer_only = true;
    want.render_scale_percent = 125;
    want.loader_chain_proxy = "other.dll";
    want.dominant_hand_left = true;
    want.movement_orientation = dsvr::MovementOrientation::Controller;
    want.movement_controller = dsvr::MovementController::Right;
    want.play_style = "roomscale";
    want.vignette_strength = 0.25f;
    want.offline_only_ack = true;
    want.sanctuary_override = true;
    want.menu_distance_m = 2.5f;
    want.menu_reading_fov_deg = 70.0f;
    want.forward_offset_m = 0.15f;
    want.eye_clearance_m = 0.3f;
    want.hide_player_model = false;
    want.hide_second_model = true;
    want.arm_ik_enabled = true;
    want.forearm_only = true;
    want.hide_ui_in_vr = true;
    want.arm_ik_log_only = false;
    want.stereo_enabled = true;
    want.afr_enabled = true;
    want.force_disable_motion_blur = true;
    want.motion_blur_original = "1";
    want.contact_speed_min = 1.2f;
    want.rehit_window_ms = 350;
    want.combat_test_bench = true;
    want.combat_test_target_m = 1.25f;
    want.weapon_separation_min_m = 0.15f;
    want.embodiment_fit = "arm";
    CHECK(dsvr::ConfigSave(path.c_str(), want));
    dsvr::Config got;
    CHECK(dsvr::ConfigLoad(path.c_str(), got));
    CHECK(got.vr_enabled && got.render_scale_percent == 125);
    CHECK(got.reshade_host);
    CHECK(got.observer_only);
    CHECK(got.loader_chain_proxy == "other.dll");
    CHECK(got.dominant_hand_left && got.play_style == "roomscale");
    CHECK(got.movement_orientation == dsvr::MovementOrientation::Controller);
    CHECK(got.movement_controller == dsvr::MovementController::Right);
    CHECK(got.vignette_strength == 0.25f && got.offline_only_ack);
    CHECK(got.sanctuary_override && got.menu_distance_m == 2.5f);
    CHECK(got.menu_reading_fov_deg == 70.0f);
    CHECK(got.forward_offset_m == 0.15f && !got.hide_player_model);
    CHECK(got.eye_clearance_m == 0.3f);
    CHECK(got.hide_second_model);
    CHECK(got.arm_ik_enabled && !got.arm_ik_log_only);
    CHECK(got.forearm_only && got.hide_ui_in_vr);
    CHECK(got.contact_speed_min == 1.2f && got.rehit_window_ms == 350);
    CHECK(got.combat_test_bench && got.combat_test_target_m == 1.25f);
    CHECK(got.weapon_separation_min_m == 0.15f);
    CHECK(got.embodiment_fit == "arm");
    CHECK(got.force_disable_motion_blur && got.motion_blur_original == "1");
    CHECK(got.stereo_enabled);
    CHECK(got.afr_enabled);
  }
  // P4 input gate: defaults ON since 0.11.0 (off = Quest controllers
  // produced no input at all), parses under both key spellings, and
  // survives the save/load round-trip.
  {
    dsvr::Config gate;
    CHECK(gate.xr_input_enabled);
    CHECK(gate.hide_second_model);
    CHECK(gate.forward_offset_m == 0.0f);
    CHECK(!gate.arm_ik_enabled && gate.arm_ik_log_only);
    CHECK(gate.contact_speed_min == 0.8f && gate.rehit_window_ms == 280);
    CHECK(!gate.combat_test_bench && gate.combat_test_target_m == 1.0f);
    CHECK(gate.weapon_separation_min_m == 0.0f);
    FILE* f = nullptr;
    CHECK(_wfopen_s(&f, path.c_str(), L"w") == 0 && f);
    fputs("[controls]\nxr_input_enabled = true\n", f);
    fclose(f);
    CHECK(dsvr::ConfigLoad(path.c_str(), gate));
    CHECK(gate.xr_input_enabled);
    CHECK(_wfopen_s(&f, path.c_str(), L"w") == 0 && f);
    fputs("[controls]\nxr_actions_enabled = true\n", f);
    fclose(f);
    CHECK(dsvr::ConfigLoad(path.c_str(), gate));
    CHECK(gate.xr_input_enabled);
    dsvr::Config want;
    want.xr_input_enabled = true;
    CHECK(dsvr::ConfigSave(path.c_str(), want));
    dsvr::Config got;
    CHECK(dsvr::ConfigLoad(path.c_str(), got));
    CHECK(got.xr_input_enabled);
  }
  {
    FILE* f = nullptr;
    CHECK(_wfopen_s(&f, path.c_str(), L"w") == 0 && f);
    fputs("[combat]\narm_ik_enabled = true\narm_ik_log_only = false\n"
          "contact_speed_min = 99\nrehit_window_ms = 1\n",
          f);
    fclose(f);
  }
  CHECK(dsvr::ConfigLoad(path.c_str(), c));
  CHECK(c.arm_ik_enabled && !c.arm_ik_log_only);
  CHECK(c.contact_speed_min == 5.0f && c.rehit_window_ms == 50);
  {
    FILE* f = nullptr;
    CHECK(_wfopen_s(&f, path.c_str(), L"w") == 0 && f);
    fputs("[vr]\nmenu_distance_m = nan\nmenu_reading_fov_deg = inf\n", f);
    fclose(f);
    CHECK(dsvr::ConfigLoad(path.c_str(), c));
    CHECK(c.menu_distance_m == dsvr::Config{}.menu_distance_m && c.menu_reading_fov_deg == dsvr::Config{}.menu_reading_fov_deg);
    CHECK(_wfopen_s(&f, path.c_str(), L"w") == 0 && f);
    fputs("[vr]\nmenu_distance_m = 99\nmenu_reading_fov_deg = 1\n", f);
    fclose(f);
    CHECK(dsvr::ConfigLoad(path.c_str(), c));
    CHECK(c.menu_distance_m == 4.0f && c.menu_reading_fov_deg == 45.0f);
  }
  DeleteFileW(path.c_str());

  printf("test_config: PASS\n");
  return 0;
}
