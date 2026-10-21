#pragma once

#include <span>

#include "combat_math.h"

namespace dsvr {

// Tracked-contact classification (T5). This layer only names what the
// tracked geometry did (hit / block / parry / nothing) so it can be
// logged and felt; applying damage, stamina, or poise to a native actor
// remains the qualified damage binding's job (CombatNativeLimitation).
// A classified WeaponHit is never, by itself, damage.
enum class ContactEvent { None, WeaponHit, ShieldBlock, Parry };

struct ContactParams {
  int parry_window_ms = 200;    // after shield raise, contact = parry
  float support_attach_m = 0.15f;  // hand-to-handle distance to attach
};

struct ContactResult {
  ContactEvent event = ContactEvent::None;
  BladeHit hit{};  // valid only when event == WeaponHit
};

inline bool ClassifyWeaponContact(const BladeKinematics& blade,
                                  GestureKind kind,
                                  std::span<const BladeContactTarget> targets,
                                  float contact_speed_min, float gain,
                                  int rehit_window_ms, long long now_ms,
                                  bool haptics_enabled, RehitGate& gate,
                                  ContactResult& out,
                                  const CombatParams& p = {}) {
  out = {};
  BladeHit hit;
  if (!EvaluateBladeContact(blade, kind, targets, contact_speed_min, gain,
                            rehit_window_ms, now_ms, haptics_enabled, gate,
                            hit, p))
    return false;
  out.event = ContactEvent::WeaponHit;
  out.hit = hit;
  return true;
}

// Defence is judged on the shield side: a raised shield that the threat
// actually reaches. Inside the parry window the same contact is a parry.
// Threat overlap/speed come from the caller's tracked geometry; without
// a native threat binding this stays a policy, not an outcome.
inline ContactEvent ClassifyDefence(bool shield_raised, long long raised_ms,
                                    long long now_ms, bool shield_overlaps,
                                    const ContactParams& p = {}) {
  if (!shield_raised || !shield_overlaps) return ContactEvent::None;
  if (now_ms >= raised_ms && now_ms - raised_ms <= p.parry_window_ms)
    return ContactEvent::Parry;
  return ContactEvent::ShieldBlock;
}

// Two-hand support grip: attaches only while the grip control is held
// with the hand near the handle; releasing detaches immediately.
struct SupportGrip {
  bool attached = false;
  bool Update(bool grip_held, float hand_to_handle_m,
              const ContactParams& p = {}) {
    attached = grip_held && hand_to_handle_m >= 0.0f &&
               hand_to_handle_m <= p.support_attach_m;
    return attached;
  }
};

}  // namespace dsvr
