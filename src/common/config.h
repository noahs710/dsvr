#pragma once
#include <string>

namespace dsvr {

enum class MovementReference : int { Head = 0, LeftHand = 1, RightHand = 2 };
enum class MovementOrientation : int { Head = 0, Controller = 1 };
enum class MovementController : int { NonDominant = 0, Left = 1, Right = 2 };

// Human-readable settings (DSVR.toml, next to the game exe).
// Defaults are safe for first load: VR stays disabled until the user opts in
// via DSVR-Setup, so a bad HMD/driver can never brick a flatscreen launch.
struct Config {
  // [loader] -- dinput8 chain-loading (QoL batch 1, item 1)
  // Filename (or path) of another dinput8-style proxy to forward to instead of
  // the system DLL, e.g. "dinput8-chain.dll". Empty = forward to System32.
  std::string loader_chain_proxy;

  // [vr]
  bool vr_enabled = false;
  // Startup-only host selection; never hot-switch an existing XR session.
  bool reshade_host = false;  // requires DSVR-ReShade.addon64 + API 18 host
  int render_scale_percent = 100;  // 50..200, eye-buffer resolution scale
  // Requested game/XR rate; the user's build/test default is 90 FPS.
  // Native limiter application and the runtime's actual display cadence are
  // separately verified; this value alone does not change either one.
  // Allowed: 60/72/90/120/144/240.
  int frame_limit_fps = 90;
  // When false, the desktop swapchain Present is skipped entirely: the
  // game still renders its backbuffer (which XR copies), but no desktop
  // composition or vsync wait is paid. The native frame limiter, not
  // Present, paces the game. Toggle live with F7 after editing.
  bool desktop_present = true;
  bool comfort_vignette = true;
  float vignette_strength = 0.65f;  // 0..1
  // In-game FOV comfort scaler: multiplies the headset's vertical FOV
  // before it is written to the game camera. 1.0 = headset truth.
  // Lower narrows the view (less motion sickness); higher widens it.
  // Live-tunable with F7; the eye buffers and headset optics never change.
  float fov_scale = 1.0f;  // 0.7..1.3
  bool snap_turn = false;
  int snap_turn_degrees = 45;  // 15..90
  bool smooth_turn = true;
  float smooth_turn_speed_dps = 240.0f;  // 30..360 deg/s
  bool head_directed_move = true;        // legacy setting; false is ignored and logged on load
  bool headlook_enabled = false;         // HMD orientation drives CameraMan
  float mono_screen_fov_deg = 60.0f;     // virtual screen width (mono mode)
  bool sanctuary_override = false;     // spatial canvas for any menu (F8)
  float menu_distance_m = 2.75f;        // 1..4 metres, independent of world scale
  float menu_reading_fov_deg = 52.0f;   // 45..80 degrees, inventory/pause
  // Installer-enforced game motion-blur off (T1). The core DLL never
  // writes the game ini; Setup forces/restores it (see blur_state.h).
  bool force_disable_motion_blur = false;
  // Native stereo caller (T3, experimental). No eye renderer is bound
  // yet, so enabling this currently logs a refusal and continues mono.
  bool stereo_enabled = false;
  // Alternating-eye mode (T4, experimental). Refused unless stereo is
  // active; see afr_policy.h.
  bool afr_enabled = false;

  // [controls]
  bool dominant_hand_left = false;       // true = left hand holds the weapon
  // Legacy migration input only (see ConfigLoad); new settings win.
  MovementReference movement_reference = MovementReference::Head;
  // Left-stick locomotion orientation. The stick is ALWAYS steered by exactly
  // one live reference: Head (default -- head-oriented smooth locomotion) or
  // Controller, which steers by movement_controller's grip heading and falls
  // back to the head reference whenever that hand is untracked or aimed
  // vertically with no stable planar heading. There is no raw/native option
  // and no dead stick: a missing hand removes hand steering, not movement.
  MovementOrientation movement_orientation = MovementOrientation::Head;
  MovementController movement_controller = MovementController::NonDominant;
  std::string play_style = "standing";   // standing | seated | roomscale
  bool hotkeys_enabled = true;
  int log_marker_vk = 0x78;              // VK_F9: timestamped marker in DSVR.log
  int config_reload_vk = 0x76;           // VK_F7: reload DSVR.toml without restart
  // OpenXR controller actions (P4 input runtime, input.cpp) + the XInput
  // pad bridge. Default ON since 0.11.0: with it off the Quest controllers
  // produced no input at all (user report, 2026-10-13). Set false to fall
  // back to a physical controller / keyboard only.
  bool xr_input_enabled = true;

  // [camera]
  float eye_height = 1.65f;       // metres above player anchor
  float world_scale = 1.0f;       // 0.5..2.0 trim on automatic native-world/avatar fit
  float forward_offset_m = 0.0f;  // 0..0.4; 0 = camera at the head (0.12.0:
                                  // the old 0.22 hovered in front of the face)
  // Height band around eye_height that the tracked eye may leave. Physical
  // kneeling / leaning moves the real eye by up to this much; anything beyond
  // it would put the eye inside the standing native torso or through the head
  // shell. 0 disables the band (tracking stays exactly physical).
  float eye_clearance_m = 0.45f;
  // First person shows the FULL body by default (0.14.0): the camera sits
  // inside the head mesh, so you see your arms, torso, legs and weapon
  // when you look down, exactly as the user asked. Hiding is now only an
  // escape hatch for headgear whose geometry blocks the view.
  bool hide_player_model = false;
  // Selective native part filtering lets the first-person camera see the body
  // without the head/helmet shell; it never collapses a bone.
  // Whole-model hiding above stays an escape hatch. Driven by the native
  // part filter while the first-person camera owns the view (VR on, not
  // observer-only, headlook on, XR session running, no menu presentation);
  // it does NOT require arm_ik_enabled. Menus keep the native head.
  bool hide_head = true;
  bool forearm_only = false;
  bool hide_ui_in_vr = false;
  // The local player owns two render models (presence.cpp): the primary at
  // PlayerIns+0x60 and a second at +0x850. The 0.11.0 test kept the second
  // visible and the user saw the player model's face in front of the
  // camera, so the second is the BODY model: hidden by default since
  // 0.12.0, primary (weapon side) kept. Set false if your weapon vanishes
  // and the body stays -- then the models are the other way round.
  bool hide_second_model = true;
  // Which native measurement anchors the automatic world/avatar fit.
  // "head" (default) sizes the world so the native Head bone lands on the
  // calibrated view height: the first-person camera sits in the avatar's own
  // head and the body occupies the space the wearer's body does. "arm" sizes
  // by measured physical reach, so the hands match exactly and the head may
  // sit off the native head bone. Both convert the whole world/avatar with one
  // ratio; neither scales a bone base or collider, and neither independently
  // resizes the skinned mesh (that still needs a verified render owner).
  std::string embodiment_fit = "head";

  // [combat]
  bool motion_attacks = true;      // motion-driven damage (no canned swings)
  float swing_speed_min = 1.5f;    // legacy [gesture] thresholds, pre-IK
  float swing_damage_gain = 1.0f;  // velocity -> damage multiplier
  bool thrust_enabled = true;
  float thrust_speed_min = 1.0f;   // legacy, pre-IK
  bool haptics = true;
  // IK arms (2026-10-13 design): the player model's own arms follow the
  // controllers. arm_ik_enabled removes weapon-hand RB/RT canned attacks
  // and the gesture pulse entirely. Until the position driver is qualified
  // in-headset, keep arm_ik_log_only=true.
  // false permits the pinned post-animation CPU-pose adapter; grip rotation
  // and weapon-grip position calibrate from the held stance, physical
  // damage is still unfinished.
  bool arm_ik_enabled = false;
  bool arm_ik_log_only = true;
  // Provisional outward roll correction (degrees, mirrored per hand) for
  // the calibrated grip: live report was hands rotated inwards a bit with
  // controllers held straight. Positive turns palms outward; set negative
  // if a live check shows the correction backwards. -45..45.
  float grip_roll_correction_deg = 12.0f;
  float contact_speed_min = 0.8f;  // m/s tip speed below which contact = 0 dmg
  int rehit_window_ms = 280;       // per-enemy minimum ms between IK hits
  // Combat proving ground (offline/headset, no native writes): when true,
  // the pose thread classifies live blade contact against a fixed
  // sparring target in front of the player and logs hit/block/parry +
  // damage scale, and the Combat panel shows live blade state. Native
  // damage is never applied; this proves the geometry before any binding.
  bool combat_test_bench = false;
  float combat_test_target_m = 1.0f;  // sparring target distance ahead (0.5..2.0)
  // Weapon/shield no-clip guard: minimum kept separation between the two
  // solved hand poses (0 = off). Flags when the blades would interpenetrate.
  float weapon_separation_min_m = 0.0f;

  // [diagnostics]
  bool verbose_logging = false;
  // Startup-only, desktop native observation. Overrides VR and all gameplay
  // adapters; changing it requires a process restart, not F7. Not save isolation.
  bool observer_only = false;

  // [safety]
  bool offline_only_ack = false;  // installer sets this after showing warning
  // Game Blur literal recorded by Setup before forcing it off ("" = none
  // recorded). Only Setup changes this value; menu saves preserve it.
  std::string motion_blur_original;
};

// Effective process config only; never rewrites the requested on-disk settings.
// Callers must preserve the startup observer_only value when reloading.
inline void ConfigRestrictObserver(Config& c) {
  if (!c.observer_only) return;
  c.vr_enabled = false;
  c.headlook_enabled = false;
  c.xr_input_enabled = false;
  c.arm_ik_enabled = false;
  c.arm_ik_log_only = true;
  c.motion_attacks = false;
  c.haptics = false;
  c.hide_player_model = false;
  c.hide_second_model = false;
  c.sanctuary_override = false;
}

// Load TOML (subset) into `out`. Missing file -> defaults + false.
bool ConfigLoad(const wchar_t* toml_path, Config& out);
// Write `cfg` as a fully commented DSVR.toml (used by installer/GUI/core).
bool ConfigSave(const wchar_t* toml_path, const Config& cfg);
bool ConfigValid(const Config& cfg);
// Write factory defaults. Equivalent to ConfigSave(path, Config{}).
bool ConfigSaveDefaults(const wchar_t* toml_path);

// Path helpers (game folder = folder of the running exe).
std::wstring GameExeDirectory();
std::wstring ConfigPathNextToExe();

// UTF-8 <-> wide conversion for filenames stored in the TOML.
std::wstring WideFromUtf8(const std::string& utf8);
std::string Utf8FromWide(const std::wstring& wide);

}  // namespace dsvr
