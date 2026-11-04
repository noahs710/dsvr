#include "../src/core/bridge_gates.h"

#include <cstdio>
#include <cstring>
#include <source_location>

namespace {
dsvr::BridgeGateState Ready() {
  dsvr::BridgeGateState s;
  s.theater_menu_active = false;
  s.camera_gameplay_ready = true;
  s.observer_only = false;
  s.arm_ik_enabled = true;
  s.arm_ik_log_only = false;
  s.arm_ik_block_animation = true;
  s.committed_hands_available = true;
  s.hand_epoch_current = true;
  s.motion_attacks = true;
  s.physical_attacks = true;
  s.physical_defence = true;
  s.weapon_hand_valid = true;
  s.shield_hand_valid = true;
  s.menu_captures_input = false;
  s.calibration_captures_input = false;
  s.capture_rearm = false;
  s.physical_damage_bound = true;
  s.shield_contact_bound = true;
  return s;
}
}  // namespace

int main() {
  int failures = 0;
  auto check = [&](bool ok, std::source_location at = std::source_location::current()) {
    if (!ok) {
      ++failures;
      std::printf("failed at line %u\n", at.line());
    }
  };
  using dsvr::BridgeGateState;
  using dsvr::BridgeGates;
  using dsvr::ComputeBridgeGates;

  // Returns the computed gates after asserting the three names.
  auto expect = [&](const BridgeGateState& s, const char* attack,
                    const char* defence, const char* suppress,
                    std::source_location at = std::source_location::current()) {
    const BridgeGates g = ComputeBridgeGates(s);
    const bool ok = std::strcmp(g.attack, attack) == 0 &&
                    std::strcmp(g.defence, defence) == 0 &&
                    std::strcmp(g.suppress, suppress) == 0;
    if (!ok) {
      ++failures;
      std::printf("line %u: got attack=%s defence=%s suppress=%s; "
                  "want %s/%s/%s\n",
                  at.line(), g.attack, g.defence, g.suppress, attack, defence,
                  suppress);
    }
    return g;
  };

  // Fully bound, current, committed hands: both bridges fire and the normal
  // buttons are suppressed in favour of the tracked outcome.
  {
    const BridgeGates g = expect(Ready(), "ready", "ready", "suppressed");
    check(g.shared == nullptr);
    check(g.attack_ready && g.defence_ready && g.suppress_on &&
          g.motion_bridge_armed);
  }

  // Shared gates, one at a time, name themselves on both bridges.
  {
    auto s = Ready();
    s.theater_menu_active = true;
    const BridgeGates g = expect(s, "theater-menu-active",
                                 "theater-menu-active", "suppressed");
    check(g.shared != nullptr && std::strcmp(g.shared, "theater-menu-active") == 0);
    s = Ready();
    s.camera_gameplay_ready = false;
    expect(s, "camera-not-gameplay", "camera-not-gameplay", "suppressed");
    s = Ready();
    s.observer_only = true;
    expect(s, "observer-only", "observer-only", "suppressed");
    s = Ready();
    s.arm_ik_enabled = false;
    expect(s, "arm_ik-off", "arm_ik-off", "arm_ik-off");
    s = Ready();
    s.arm_ik_log_only = true;
    expect(s, "arm_ik-log-only", "arm_ik-log-only", "arm_ik-log-only");
    s = Ready();
    s.arm_ik_block_animation = false;
    expect(s, "block_animation-off", "block_animation-off", "suppressed");
    s = Ready();
    s.committed_hands_available = false;
    expect(s, "no-committed-hands", "no-committed-hands", "suppressed");
    s = Ready();
    s.hand_epoch_current = false;
    expect(s, "hand-epoch-stale", "hand-epoch-stale", "suppressed");
  }

  // Shared-gate precedence: the earliest gate wins over later ones.
  {
    auto s = Ready();
    s.theater_menu_active = true;
    s.camera_gameplay_ready = false;
    s.observer_only = true;
    expect(s, "theater-menu-active", "theater-menu-active", "suppressed");
    s = Ready();
    s.arm_ik_log_only = true;
    s.committed_hands_available = false;
    expect(s, "arm_ik-log-only", "arm_ik-log-only", "arm_ik-log-only");
  }

  // Attack-specific gates leave defence alone, and vice versa.
  {
    auto s = Ready();
    s.motion_attacks = false;
    check(expect(s, "motion_attacks-off", "ready", "suppressed").shared == nullptr);
    s = Ready();
    s.physical_attacks = false;
    expect(s, "physical_attacks-off", "ready", "suppressed");
    s = Ready();
    s.weapon_hand_valid = false;
    expect(s, "weapon-hand-invalid", "ready", "suppressed");
    s = Ready();
    s.physical_defence = false;
    expect(s, "ready", "physical_defence-off", "suppressed");
    s = Ready();
    s.shield_hand_valid = false;
    expect(s, "ready", "shield-hand-invalid", "suppressed");
  }

  // Suppression is independent of the bridge gates: it is about replacing the
  // normal game buttons, not about whether a tracked hand is usable.
  {
    auto s = Ready();
    s.theater_menu_active = true;
    const BridgeGates g = expect(s, "theater-menu-active", "theater-menu-active",
                                 "suppressed");
    check(g.suppress_on);
    s = Ready();
    s.committed_hands_available = false;
    check(ComputeBridgeGates(s).suppress_on);
  }

  // Suppression precedence and condition.
  {
    auto s = Ready();
    s.arm_ik_enabled = false;
    check(!expect(s, "arm_ik-off", "arm_ik-off", "arm_ik-off").suppress_on);
    s = Ready();
    s.arm_ik_log_only = true;
    check(!expect(s, "arm_ik-log-only", "arm_ik-log-only", "arm_ik-log-only")
               .suppress_on);
    s = Ready();
    s.physical_damage_bound = false;
    check(!expect(s, "ready", "ready", "damage-unbound").suppress_on);
    s = Ready();
    s.shield_contact_bound = false;
    check(!expect(s, "ready", "ready", "shield-unbound").suppress_on);
  }

  // The log-only arming flag watches menu/calibration/rearm and nothing else.
  {
    auto s = Ready();
    s.camera_gameplay_ready = false;
    check(ComputeBridgeGates(s).motion_bridge_armed);
    s = Ready();
    s.theater_menu_active = true;
    check(!ComputeBridgeGates(s).motion_bridge_armed);
    s = Ready();
    s.motion_attacks = false;
    check(!ComputeBridgeGates(s).motion_bridge_armed);
    s = Ready();
    s.menu_captures_input = true;
    check(!ComputeBridgeGates(s).motion_bridge_armed);
    s = Ready();
    s.calibration_captures_input = true;
    check(!ComputeBridgeGates(s).motion_bridge_armed);
    s = Ready();
    s.capture_rearm = true;
    check(!ComputeBridgeGates(s).motion_bridge_armed);
  }

  // Defaults fail closed.
  {
    const BridgeGates g = expect(BridgeGateState{}, "theater-menu-active",
                                 "theater-menu-active", "arm_ik-off");
    check(!g.attack_ready && !g.defence_ready && !g.suppress_on &&
          !g.motion_bridge_armed);
  }

  if (!failures) std::printf("bridge_gates: checks passed\n");
  return failures ? 1 : 0;
}
