#pragma once

#include <cstdint>

namespace dsvr {
struct CombatEquipment {
  std::uintptr_t player = 0;
  int weapon_id[2]{-1, -1};  // physical left/right, actual equipped PARAM IDs
  int native_stance = 0;    // native assembly state; never written by this adapter
};
struct CombatNativeStatus {
  bool equipment_bound = false;
  bool physical_damage_bound = false;
  bool shield_contact_bound = false;
  bool two_hand_rules_bound = false;
  CombatEquipment equipment{};
};

// Pose-owner thread, expected player validated by the caller. Read-only and
// signature gated; no speculative native function calls or equipment writes.
bool CombatNativeReadEquipment(std::uintptr_t expected_player, CombatEquipment& out);
void CombatNativeInvalidateEquipment();
CombatNativeStatus CombatNativeGetStatus();
// Honest user-visible capability status. Native damage/guard/parry remain
// unavailable until their actor/contact ABI and outcome attribution are traced.
const char* CombatNativeLimitation();
}  // namespace dsvr
