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
  // PID-unique so parallel ctest runs cannot share/trample one file.
  std::wstring path = std::wstring(tmp) + L"DSVR-test-config-" +
      std::to_wstring(GetCurrentProcessId()) + L".toml";
  DeleteFileW(path.c_str());

  // Save defaults, then load them back.
  CHECK(dsvr::ConfigSaveDefaults(path.c_str()));
  dsvr::Config c;
  CHECK(dsvr::ConfigLoad(path.c_str(), c));
  CHECK(!c.vr_enabled);
  CHECK(!c.reshade_host);
  CHECK(!c.observer_only);
  CHECK(!c.autonomous_session_capture);
  {
    dsvr::Config capture{};
    capture.autonomous_session_capture = true;
    capture.vr_enabled = true;
    capture.xr_input_enabled = true;
    CHECK(!dsvr::ConfigAutonomousSessionCaptureAllowed(capture, false));
    CHECK(dsvr::ConfigAutonomousSessionCaptureAllowed(capture, true));
    capture.observer_only = true;
    CHECK(!dsvr::ConfigAutonomousSessionCaptureAllowed(capture, true));
    capture.observer_only = false;
    capture.vr_enabled = false;
    CHECK(!dsvr::ConfigAutonomousSessionCaptureAllowed(capture, true));
    capture.vr_enabled = true;
    capture.xr_input_enabled = false;
    CHECK(dsvr::ConfigAutonomousSessionCaptureAllowed(capture, true));
    capture.xr_input_enabled = true;
    capture.combat_trace_only = true;
    CHECK(!dsvr::ConfigAutonomousSessionCaptureAllowed(capture, true));
  }
  CHECK(c.render_scale_percent == 100);
  CHECK(c.snap_turn_degrees == 45);
  CHECK(c.movement_orientation == dsvr::MovementOrientation::Head);
  CHECK(c.movement_controller == dsvr::MovementController::NonDominant);
  CHECK(c.eye_height == 1.65f && !c.eye_height_override);
  CHECK(c.seated_height_offset_m == .5f);
  CHECK(c.forward_offset_m == 0.0f && !c.hide_player_model);
  CHECK(c.eye_clearance_m == 0.45f);
  CHECK(c.hide_head);
  CHECK(c.embodiment_fit == "head");
  CHECK(c.motion_attacks);
  CHECK(!c.offline_only_ack);
  CHECK(!c.theater_override && c.menu_distance_m == 2.75f);
  CHECK(c.menu_reading_fov_deg == 52.0f);
  CHECK(c.frame_limit_fps == 0);
  CHECK(c.auto_frame_limit_fps);
  CHECK(c.grip_roll_correction_deg == 12.0f);
  CHECK(!c.force_disable_motion_blur && c.motion_blur_original.empty());
  CHECK(!c.stereo_enabled);
  CHECK(!c.afr_enabled);
  // Immersive first person is the shipped gameplay view: headlook is on and
  // the manual theater escape is off. Theater is chosen by menus, the title
  // screen and cutscenes, or by hand (gameplay_theater / F8).
  CHECK(c.headlook_enabled);
  CHECK(!c.gameplay_theater);
  CHECK(!c.desktop_present);
  CHECK(dsvr::FactorySafeDefaults().headlook_enabled);
  CHECK(!dsvr::FactorySafeDefaults().gameplay_theater);
  CHECK(!dsvr::FactorySafeDefaults().desktop_present);
  // Every key the file writes must be a key the loader recognises: a written
  // key nobody reads is a promise the setup would break. ConfigSave and
  // ConfigLoad are checked against each other by round-tripping a config whose
  // booleans are all flipped (below), and the written text must name the new
  // gameplay-view key.
  {
    FILE* f = _wfopen(path.c_str(), L"rb");
    CHECK(f != nullptr);
    std::string text;
    if (f) {
      char buf[4096];
      size_t got = 0;
      while ((got = fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, got);
      fclose(f);
    }
    CHECK(text.find("gameplay_theater") != std::string::npos);
  CHECK(text.find("desktop_present") != std::string::npos);
    CHECK(text.find("headlook_enabled = true") != std::string::npos);
        CHECK(text.find("frame_limit_fps") == std::string::npos);
        CHECK(text.find("auto_frame_limit_fps") == std::string::npos);
  }

  // Hand-edited values.
  {
    FILE* f = nullptr;
    CHECK(_wfopen_s(&f, path.c_str(), L"w") == 0 && f);
    fputs("[vr]\nvr_enabled = true\nreshade_host = true\nrender_scale_percent = 150\n"
          "frame_limit_fps = 144\nauto_frame_limit_fps = false\n"
          "snap_turn_degrees = 30\nhead_directed_move = false\n"
          "sanctuary_override = true\n"  // legacy name for theater_override
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
  CHECK(c.frame_limit_fps == 0);
  CHECK(c.auto_frame_limit_fps);
  CHECK(c.snap_turn_degrees == 30);
  CHECK(c.movement_orientation == dsvr::MovementOrientation::Head);
  CHECK(!c.head_directed_move);
  CHECK(c.theater_override);  // legacy sanctuary_override key still parses
  CHECK(c.eye_height == 1.8f && !c.eye_height_override); // legacy value is not opt-in
  c.eye_height_override=true;
  c.seated_height_offset_m=.73f;
  CHECK(dsvr::ConfigSave(path.c_str(),c));
  CHECK(dsvr::ConfigLoad(path.c_str(),c) && c.eye_height_override && c.eye_height==1.8f);
  CHECK(c.seated_height_offset_m==.73f);
  CHECK(c.world_scale == 1.2f);
  CHECK(c.forward_offset_m == 0.12f && !c.hide_player_model);
  CHECK(c.eye_clearance_m == 0.6f);
  CHECK(!c.hide_head);
  CHECK(c.embodiment_fit == "head");  // legacy "arm" spelling: combined fit
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

  // A turning mode has to stay live. With both turn modes off the turn
  // tracker has nothing to integrate and the stick is silently dead, so the
  // loader restores one rather than shipping a control that does nothing.
  {
    FILE* f = nullptr;
    CHECK(_wfopen_s(&f, path.c_str(), L"w") == 0 && f);
    fputs("[vr]\nsnap_turn = false\nsmooth_turn = false\nsnap_turn_degrees = 30\n"
          "smooth_turn_speed_dps = 120\n",
          f);
    fclose(f);
  }
  CHECK(dsvr::ConfigLoad(path.c_str(), c));
  CHECK(c.snap_turn || c.smooth_turn);
  CHECK(c.snap_turn_degrees == 30);      // untouched comfort values
  CHECK(c.smooth_turn_speed_dps == 120.0f);
  // One mode on is a choice, not a mistake: the loader must not force both.
  {
    FILE* f = nullptr;
    CHECK(_wfopen_s(&f, path.c_str(), L"w") == 0 && f);
    fputs("[vr]\nsnap_turn = true\nsmooth_turn = false\n", f);
    fclose(f);
  }
  CHECK(dsvr::ConfigLoad(path.c_str(), c));
  CHECK(c.snap_turn && !c.smooth_turn);

  // Missing file keeps defaults and reports false.
  DeleteFileW(path.c_str());
  CHECK(!dsvr::ConfigLoad(path.c_str(), c));
  CHECK(!c.vr_enabled && c.render_scale_percent == 100);

  // Observer mode is a startup-only desktop path, never a VR companion:
  // the effective process config must refuse any mix, so a stale or
  // hand-edited file can never boot flatscreen while claiming VR.
    {
    dsvr::Config mixed{};
    mixed.vr_enabled = true;
    mixed.observer_only = true;
    CHECK(!dsvr::ConfigValid(mixed));
    mixed.observer_only = false;
    mixed.combat_trace_only = true;
    CHECK(!dsvr::ConfigValid(mixed));
    mixed.combat_trace_only = false;
    mixed.vr_enabled = false;
    mixed.observer_only = true;
    CHECK(dsvr::ConfigValid(mixed));
    }

    // Factory-safe defaults: the average-user shipping posture. VR on, Auto
    // Smooth cadence, vignette/snap off (calm baseline), honest features.
    {
    const dsvr::Config safe = dsvr::FactorySafeDefaults();
    CHECK(safe.vr_enabled && !safe.observer_only);
    CHECK(safe.frame_limit_fps == 0 && safe.auto_frame_limit_fps);
    CHECK(safe.render_scale_percent == 100);
    CHECK(!safe.comfort_vignette && safe.vignette_strength == 0.65f);
    CHECK(!safe.snap_turn && safe.snap_turn_degrees == 45);
    CHECK(safe.smooth_turn && safe.smooth_turn_speed_dps == 360.0f);
      CHECK(dsvr::Config{}.smooth_turn_speed_dps == 360.0f);
    CHECK(safe.play_style == "standing" && safe.head_directed_move);
    CHECK(safe.movement_orientation == dsvr::MovementOrientation::Head);
    CHECK(safe.movement_controller == dsvr::MovementController::NonDominant);
    CHECK(!safe.dominant_hand_left && safe.embodiment_fit == "head");
    CHECK(!safe.arm_ik_enabled && safe.arm_ik_log_only && safe.arm_ik_block_animation);
    CHECK(safe.motion_attacks && safe.haptics);
    CHECK(!safe.physical_attacks && !safe.physical_defence && !safe.combat_test_bench);
    CHECK(!safe.autonomous_session_capture && !safe.verbose_logging);
    CHECK(!safe.reach_interact && !safe.arm_swing_sprint && !safe.stereo_enabled);
    CHECK(!safe.combat_trace_only && !safe.hide_player_model && safe.hide_second_model);
    CHECK(dsvr::ConfigValid(safe));
    CHECK(dsvr::ConfigSave(path.c_str(), safe));
    CHECK(dsvr::ConfigLoad(path.c_str(), c) && c == safe);
  }

  // Manual theater is a persisted choice, not only a live F8 override: it must
  // survive a save/load cycle, and the immersive default must survive it too.
  {
    dsvr::Config view = dsvr::FactorySafeDefaults();
    view.gameplay_theater = true;
    CHECK(dsvr::ConfigValid(view));
    CHECK(dsvr::ConfigSave(path.c_str(), view));
    dsvr::Config back{};
    CHECK(dsvr::ConfigLoad(path.c_str(), back));
    CHECK(back.gameplay_theater);
    CHECK(back.headlook_enabled);
    CHECK(back == view);
    view.gameplay_theater = false;
    CHECK(dsvr::ConfigSave(path.c_str(), view));
    CHECK(dsvr::ConfigLoad(path.c_str(), back));
    CHECK(!back.gameplay_theater);
  }

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

  // A legacy "arm" file yields the combined fit; a typo keeps the default,
  // the same way an unknown play_style does: neither silently disables it.
  {
    FILE* f = nullptr;
    CHECK(_wfopen_s(&f, path.c_str(), L"w") == 0 && f);
    fputs("[camera]\nembodiment_fit = \"arm\"\n", f);  // legacy: combined fit
    fclose(f);
  }
  CHECK(dsvr::ConfigLoad(path.c_str(), c));
  CHECK(c.embodiment_fit == "head");
  {
    FILE* f = nullptr;
    CHECK(_wfopen_s(&f, path.c_str(), L"w") == 0 && f);
    fputs("[camera]\nembodiment_fit = \"knees\"\n", f);  // typo: default
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
    want.vr_enabled = false;  // observer mode is desktop-only; VR mix is refused
    want.reshade_host = true;
    want.observer_only = true;
    want.autonomous_session_capture = true;
    want.render_scale_percent = 125;
    want.loader_chain_proxy = "other.dll";
    want.dominant_hand_left = true;
    want.movement_orientation = dsvr::MovementOrientation::Controller;
    want.movement_controller = dsvr::MovementController::Right;
    want.play_style = "roomscale";
    want.vignette_strength = 0.25f;
    want.offline_only_ack = true;
    want.theater_override = true;
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
    // Both new keys are exercised away from their default, so a dropped
    // parse or save line shows up as a round-trip failure.
    want.hide_torso_and_legs = true;
    want.arm_ik_block_animation = false;
    want.desktop_present = true;
    want.physical_two_hand = true;
    want.physical_skills = true;
    want.cast_aim = true;
    want.cast_aim_seconds = 2.0f;
    want.stereo_enabled = true;
    want.afr_enabled = true;
    want.aa_mode = 1;
    want.force_disable_motion_blur = true;
    want.motion_blur_original = "1";
    want.contact_speed_min = 1.2f;
    want.rehit_window_ms = 350;
    want.combat_test_bench = true;
    want.combat_test_target_m = 1.25f;
    want.weapon_separation_min_m = 0.15f;
    want.physical_attacks = true;
    // Per-profile bindings are exercised off-default too: a profile switched off
    // must survive the round-trip or a saved setup silently re-binds it.
    want.bind_profile_wmr = false;
    want.binding_live_reattach = true;
    want.swing_attack_speed_min = 2.6f;
    want.swing_attack_heavy_min = 6.0f;
    
    want.physical_defence = true;
    want.guard_settle_ms = 200;
    want.guard_flick_speed_min = 3.0f;
    want.guard_parry_press_ms = 120;
    want.guard_parry_cooldown_ms = 900;
    want.weapon_mass_table_path = "mass.tbl";
    want.torque_response = 1.5f;
    want.halfsword_envelope_m = 0.2f;
    want.deflect_amplitude = 0.7f;
    want.deflect_duration_ms = 40;
    want.embodiment_fit = "head";  // combined fit: the only value written
    CHECK(dsvr::ConfigSave(path.c_str(), want));
    dsvr::Config got;
    CHECK(dsvr::ConfigLoad(path.c_str(), got));
    CHECK(!got.vr_enabled && got.render_scale_percent == 125);
    CHECK(got.reshade_host);
    CHECK(got.observer_only);
    CHECK(got.autonomous_session_capture);
    CHECK(got.loader_chain_proxy == "other.dll");
    CHECK(got.dominant_hand_left && got.play_style == "roomscale");
    CHECK(got.movement_orientation == dsvr::MovementOrientation::Controller);
    CHECK(got.movement_controller == dsvr::MovementController::Right);
    CHECK(got.vignette_strength == 0.25f && got.offline_only_ack);
    CHECK(got.theater_override && got.menu_distance_m == 2.5f);
    CHECK(got.menu_reading_fov_deg == 70.0f);
    CHECK(got.forward_offset_m == 0.15f && !got.hide_player_model);
    CHECK(got.eye_clearance_m == 0.3f);
    CHECK(got.hide_second_model);
    CHECK(got.arm_ik_enabled && !got.arm_ik_log_only);
    CHECK(got.hide_torso_and_legs && !got.arm_ik_block_animation);
    CHECK(got.desktop_present);
    CHECK(got.physical_two_hand);
    CHECK(got.physical_skills);
    CHECK(got.cast_aim && got.cast_aim_seconds == 2.0f);
    CHECK(got.forearm_only && got.hide_ui_in_vr);
    CHECK(got.contact_speed_min == 1.2f && got.rehit_window_ms == 350);
    CHECK(got.combat_test_bench && got.combat_test_target_m == 1.25f);
    CHECK(got.weapon_separation_min_m == 0.15f);
    CHECK(got.physical_attacks && got.swing_attack_speed_min == 2.6f);
    CHECK(!got.bind_profile_wmr && got.binding_live_reattach);
    CHECK(got.bind_profile_quest3 && got.bind_profile_index &&
          got.bind_profile_simple);
    CHECK(got.swing_attack_heavy_min == 6.0f);
    CHECK(got.physical_defence && got.guard_settle_ms == 200);
    CHECK(got.guard_flick_speed_min == 3.0f && got.guard_parry_press_ms == 120);
    CHECK(got.guard_parry_cooldown_ms == 900);
    CHECK(got.weapon_mass_table_path == "mass.tbl");
    CHECK(got.torque_response == 1.5f);
    CHECK(got.halfsword_envelope_m == 0.2f);
    CHECK(got.deflect_amplitude == 0.7f);
    CHECK(got.deflect_duration_ms == 40);
    CHECK(got.embodiment_fit == "head");  // "arm" now yields the combined fit
    CHECK(got.force_disable_motion_blur && got.motion_blur_original == "1");
    CHECK(got.stereo_enabled);
    CHECK(got.afr_enabled);
    CHECK(got.aa_mode == 1);
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
    CHECK(!gate.physical_attacks && gate.swing_attack_speed_min == 2.0f);
    CHECK(gate.bind_profile_quest3 && gate.bind_profile_index &&
          gate.bind_profile_wmr && gate.bind_profile_simple);
    CHECK(!gate.binding_live_reattach);
    CHECK(gate.swing_attack_heavy_min == 4.5f);
    
    CHECK(!gate.physical_defence && gate.guard_settle_ms == 0);
    CHECK(gate.guard_flick_speed_min == 2.2f && gate.guard_parry_press_ms == 90);
    CHECK(gate.guard_parry_cooldown_ms == 700);
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
  // Legacy rate keys are ignored and not serialized.
  c.frame_limit_fps = 80;
  c.auto_frame_limit_fps = false;
  CHECK(dsvr::ConfigValid(c));
  CHECK(dsvr::ConfigSave(path.c_str(), c));
  CHECK(dsvr::ConfigLoad(path.c_str(), c) && c.frame_limit_fps == 0 &&
        c.auto_frame_limit_fps);
  // Last-known headset sidecar: the game records the shipped refresh rate
  // beside the toml; the launcher reads it without probing any runtime.
  {
    const std::wstring sidecar = std::wstring(path) + L".headset-hz";
    DeleteFileW(sidecar.c_str());
    CHECK(dsvr::ReadLastHeadsetHz(sidecar.c_str()) == 0);  // absent = unknown
    CHECK(dsvr::WriteLastHeadsetHz(sidecar.c_str(), 53));
          CHECK(dsvr::ReadLastHeadsetHz(sidecar.c_str()) == 53);
          CHECK(dsvr::WriteLastHeadsetHz(sidecar.c_str(), 120));
    CHECK(dsvr::ReadLastHeadsetHz(sidecar.c_str()) == 120);
    CHECK(dsvr::WriteLastHeadsetHz(sidecar.c_str(), 0));  // never persist unknown
    CHECK(dsvr::ReadLastHeadsetHz(sidecar.c_str()) == 120);
    CHECK(dsvr::WriteLastHeadsetHz(sidecar.c_str(), -90));  // negative is unknown too
    CHECK(dsvr::ReadLastHeadsetHz(sidecar.c_str()) == 120);
    DeleteFileW(sidecar.c_str());
  }
  DeleteFileW(path.c_str());

  printf("test_config: PASS\n");
  return 0;
}
