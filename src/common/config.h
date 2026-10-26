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
  bool operator==(const Config&) const = default;
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
  // Arm-swing locomotion (arm_swing_math.h): swinging the arms in loose
  // alternation, the way a body runs, asks the native game to run. The output
  // is one forward amount that composes with the stick and roomscale input in
  // the same movement basis, so native acceleration, stamina drain, turning
  // and the dash all stay the game's -- holding the stick forward still dashes
  // exactly as before. Speeds are head-relative, so riding a lift or walking
  // the room cannot manufacture a stride, and one hand flapping or a
  // two-handed swing is not a gait (both put the hands in motion together).
  bool arm_swing_sprint = false;
  float arm_swing_stride_min = 1.6f;  // m/s hand speed that counts as a stride
  float arm_swing_stride_max = 8.0f;  // m/s above which this is a swing, not a stride
  float arm_swing_quiet_max = 0.9f;   // m/s at or below which the other hand is quiet
  int arm_swing_alternations = 3;     // hand swaps before the run engages
  int arm_swing_hold_ms = 600;        // ms the run lingers after the last stride
  // Reach-to-interact: thrusting one hand forward through the reach envelope
  // requests the native interact button. The eligibility gate in input.cpp
  // refuses untargeted gestures until a native prompt/LOS owner exists, so
  // enabling this today only logs the refusal. Off by default until a live
  // target producer qualifies an actual interaction class.
  bool reach_interact = false;
  float reach_forward_min_m = 0.75f;  // m ahead of the head the hand must reach
  float reach_forward_max_m = 2.2f;   // m ahead of the head beyond which nothing counts
  float reach_speed_min = 1.2f;       // m/s forward hand speed that reads as a thrust
  float reach_dominance_min = 0.6f;   // fraction of hand speed that must point forward
  float reach_quiet_max = 0.8f;       // m/s a hand must stay below to count as still
  int reach_settle_ms = 250;          // ms both hands must rest before a thrust can count
  int reach_press_ms = 60;            // ms the interact tap is held
  int reach_cooldown_ms = 900;        // ms before the same hand may reach again
  bool hotkeys_enabled = true;
  int log_marker_vk = 0x78;              // VK_F9: timestamped marker in DSVR.log
  int config_reload_vk = 0x76;           // VK_F7: reload DSVR.toml without restart
  // OpenXR controller actions (P4 input runtime, input.cpp) + the XInput
  // pad bridge. Default ON since 0.11.0: with it off the Quest controllers
  // produced no input at all (user report, 2026-10-13). Set false to fall
  // back to a physical controller / keyboard only.
  bool xr_input_enabled = true;

  // [controls] per-profile suggested bindings (input.cpp SuggestAll). A profile
  // that is off is never suggested, so the runtime cannot resolve a binding the
  // operator removed. WMR carries the full set (poses, squeeze, trigger, stick,
  // haptics, menu, stick clicks); Quest 3 and Index carry their own face-button
  // rows; the KHR simple profile is the pose/haptic-only fallback that lets an
  // unknown device still drive the hands. Leave the fallback on unless every
  // device in the room is known.
  bool bind_profile_quest3 = true;
  bool bind_profile_index = true;
  bool bind_profile_wmr = true;
  bool bind_profile_simple = true;
  // Legacy preference retained for profile compatibility. Binding edits always
  // require a restart: OpenXR permits only one action-set attach per session.
  bool binding_live_reattach = false;

  // [camera]
  // Calibration owns the default. Legacy eye_height alone is a fallback, not
  // an implicit override. Only the in-game menu exposes this opt-in.
  bool eye_height_override = false;
  float eye_height = 1.65f;       // manual/fallback metres above player anchor
  float seated_height_offset_m = .5f; // explicit view lift; never a stature/reach measurement
  float world_scale = 1.0f;       // 0.5..2.0 trim on automatic native-world/avatar fit
  // Avatar size trim (0.6..1.6): a second multiplier on the SAME applied camera
  // snapshot, so the head, hands, roomscale and the IK body all read one value
  // and nothing detaches from anything else. Like world_scale it changes the
  // apparent size of the whole world/avatar together through one ratio; it is
  // NOT an independent skinned-mesh resize, which still needs a verified native
  // render owner, and it never scales a bone base or a collider.
  float avatar_scale = 1.0f;
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
  // Torso and legs can be excluded on their own while the arms stay: the
  // native part table keeps body (slot 3) and legs (slot 5) as separate
  // records from arms (slot 4), so this clears part types only and never a
  // bone transform. The weapon lives on the primary render model, which this
  // filter never touches.
  bool hide_torso_and_legs = false;
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
  // Animation ownership. The IK-owned subtrees (both arm chains, the torso
  // chain and the head) are written only by the IK: each frame their source
  // rows restart from a frozen upright native reference palette captured once
  // per rig revision, so no clip, blend, attack or idle animation can move an
  // IK bodypart. The native animation writer still runs, so animation state,
  // event counters, legs, equipment and every non-owned row stay native.
  // false restores the previous post-animation retarget.
  bool arm_ik_block_animation = true;
  // Provisional outward roll correction (degrees, mirrored per hand) for
  // the calibrated grip: live report was hands rotated inwards a bit with
  // controllers held straight. Positive turns palms outward; set negative
  // if a live check shows the correction backwards. -45..45.
  float grip_roll_correction_deg = 12.0f;
  float contact_speed_min = 0.8f;  // m/s tip speed below which contact = 0 dmg
  int rehit_window_ms = 280;       // per-enemy minimum ms between IK hits
  // Opt-in native attack-input preview, not a physical-damage binding.
  // Requires a fresh committed weapon-hand IK pose with animation ownership.
  // Rest then sustained head-relative blade speed requests one native attack;
  // speed selects light/strong. Thrust bars are 0.75x when thrusts are enabled.
  // Native animation windows, collision and outcomes remain unqualified.
  // Keep manual combat controls and leave this off until the live gate passes.
  bool physical_attacks = false;
  float swing_attack_speed_min = 2.0f;  // m/s tip speed for a light slash
  float swing_attack_heavy_min = 4.5f;  // m/s tip speed for a strong slash
  int swing_attack_cooldown_ms = 500;   // ms before the same hand attacks again
  // Physical defence bridge: the mirror of the attack bridge for the shield
  // hand. A raised, still hand holds native block (LB); a qualified flick
  // taps native parry (LT). Same qualification limits:
  // native guard windows, chip damage and parry judgement stay unqualified.
  bool physical_defence = false;
  int guard_settle_ms = 120;             // ms a raised hand must stay still
  float guard_flick_speed_min = 2.2f;    // m/s hand speed that reads as a flick
  int guard_parry_press_ms = 90;         // ms the parry tap is held
  int guard_parry_cooldown_ms = 700;     // ms before the same hand may parry again
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
  // Startup-only LAB observer domain: native contact + Step only, no renderer
  // probes. Broad constant-upload tracing overwhelmed the first B1 capture.
  bool combat_trace_only = false;

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
