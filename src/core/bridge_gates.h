#pragma once

namespace dsvr {

// Tracked-swing bridge decision. The tracked attack and defence bridges request
// ordinary native input only; these gates decide whether a committed IK hand
// may feed them, and when the normal game buttons must stay live instead.
//
// Pure: no config struct, no native status, no clock, so every precedence is
// unit-testable. The caller maps live state into the struct.
struct BridgeGateState {
  // Shared preconditions, in precedence order.
  bool theater_menu_active = true;
  bool camera_gameplay_ready = false;
  bool observer_only = true;
  bool arm_ik_enabled = false;
  bool arm_ik_log_only = true;
  bool arm_ik_block_animation = true;
  bool committed_hands_available = false;
  bool hand_epoch_current = false;
  // Attack- and defence-specific, checked after the shared gates.
  bool motion_attacks = false;
  bool physical_attacks = false;
  bool physical_defence = false;
  bool weapon_hand_valid = false;
  bool shield_hand_valid = false;
  // Arming flag for the input bridge (log only): a menu, calibration or rearm
  // window means the hands must not drive combat this frame.
  bool menu_captures_input = true;
  bool calibration_captures_input = true;
  bool capture_rearm = true;
  // Native outcome bindings. The normal buttons stay live until both are
  // proven, so a log-only or partially bound bridge never removes controls.
  bool physical_damage_bound = false;
  bool shield_contact_bound = false;
};

struct BridgeGates {
  // The shared bridge gate, nullptr when every committed hand is usable.
  const char* shared = nullptr;
  const char* attack = "uncomputed";
  const char* defence = "uncomputed";
  const char* suppress = "uncomputed";
  bool attack_ready = false;
  bool defence_ready = false;
  bool suppress_on = false;
  bool motion_bridge_armed = false;
};

inline BridgeGates ComputeBridgeGates(const BridgeGateState& s) {
  BridgeGates out;
  const char* shared = nullptr;
  if (s.theater_menu_active) shared = "theater-menu-active";
  else if (!s.camera_gameplay_ready) shared = "camera-not-gameplay";
  else if (s.observer_only) shared = "observer-only";
  else if (!s.arm_ik_enabled) shared = "arm_ik-off";
  else if (s.arm_ik_log_only) shared = "arm_ik-log-only";
  else if (!s.arm_ik_block_animation) shared = "block_animation-off";
  else if (!s.committed_hands_available) shared = "no-committed-hands";
  else if (!s.hand_epoch_current) shared = "hand-epoch-stale";
  out.shared = shared;

  if (shared) {
    out.attack = shared;
    out.defence = shared;
  } else {
    if (!s.motion_attacks) out.attack = "motion_attacks-off";
    else if (!s.physical_attacks) out.attack = "physical_attacks-off";
    else if (!s.weapon_hand_valid) out.attack = "weapon-hand-invalid";
    else out.attack = "ready";
    if (!s.physical_defence) out.defence = "physical_defence-off";
    else if (!s.shield_hand_valid) out.defence = "shield-hand-invalid";
    else out.defence = "ready";
  }
  out.attack_ready =
      !shared && s.motion_attacks && s.physical_attacks && s.weapon_hand_valid;
  out.defence_ready = !shared && s.physical_defence && s.shield_hand_valid;

  // Suppression is independent of the bridge gates: it is about whether the
  // normal game buttons may be replaced, not whether a tracked hand is usable.
  out.suppress_on = s.arm_ik_enabled && !s.arm_ik_log_only &&
      s.physical_damage_bound && s.shield_contact_bound;
  if (!s.arm_ik_enabled) out.suppress = "arm_ik-off";
  else if (s.arm_ik_log_only) out.suppress = "arm_ik-log-only";
  else if (!s.physical_damage_bound) out.suppress = "damage-unbound";
  else if (!s.shield_contact_bound) out.suppress = "shield-unbound";
  else out.suppress = "suppressed";

  out.motion_bridge_armed = !s.theater_menu_active && s.motion_attacks &&
      !s.menu_captures_input && !s.calibration_captures_input && !s.capture_rearm;
  return out;
}

}  // namespace dsvr
