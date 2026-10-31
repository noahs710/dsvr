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
  float support_attach_m = 0.15f;  // hand-to-blade-segment distance to attach
  float support_release_m = 0.20f;  // detach distance (hysteresis above attach)
  // Steering (mesh, not hitbox): a mid-blade halfsword grab must be ON the
  // blade surface, not floating beside it in the envelope tube. Allowed
  // distance lerps from support_attach_m at the hilt to support_blade_m at
  // the tip: the hilt keeps the generous grab envelope (hand wraps the
  // grip/guard), the blade requires mesh contact.
  float support_blade_m = 0.06f;  // tip-end attach distance (mesh contact)
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
                                  const CombatParams& p = {},
                                    float torque_response = 1.0f) {
  out = {};
  BladeHit hit;
  if (!EvaluateBladeContact(blade, kind, targets, contact_speed_min, gain,
                            rehit_window_ms, now_ms, haptics_enabled, gate,
                            hit, p, torque_response))
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
  if (!shield_raised || !shield_overlaps || raised_ms < 0 || now_ms < raised_ms ||
      p.parry_window_ms < 0) return ContactEvent::None;
  if (p.parry_window_ms > 0 && now_ms - raised_ms <= p.parry_window_ms)
    return ContactEvent::Parry;
  return ContactEvent::ShieldBlock;
}

// Two-hand support grip (slice 1): attaches while the free-hand squeeze is
// held and the free grip is near ANY point of the committed blade segment —
// hilt for a full two-hand, mid-blade for a halfsword. Segment-to-point, not
// a grip-point proxy: the old 14 cm handle rule detached mid-blade grabs.
// Release on squeeze release or leaving the release envelope (hysteresis
// above the attach distance so boundary hover cannot chatter). Segment points
// are in native world units; pass native units per metre to scale the envelope.
// No latch: attach/release depend only on current inputs.
struct SupportGrip {
  bool attached = false;
  // Fraction along the segment the grab holds (0 = grip, 1 = tip). Valid
  // only while attached; drives the halfsword contact shortening.
  float grab_fraction = 0.0f;
  bool Update(bool grip_held, Vec3 free_grip, Vec3 blade_grip, Vec3 blade_tip,
              const ContactParams& p = {},
              float native_units_per_meter = 1.0f) {
    if (!grip_held || !FiniteVec(free_grip) || !FiniteVec(blade_grip) ||
        !FiniteVec(blade_tip) || !std::isfinite(native_units_per_meter) ||
        !(native_units_per_meter > 0.0f) || !(p.support_attach_m >= 0.0f) ||
        !(p.support_release_m >= p.support_attach_m) ||
        !(p.support_blade_m >= 0.0f) ||
        !(p.support_blade_m <= p.support_attach_m)) {
      attached = false;
      return attached;
    }
    const float d = SegmentPointDistance(blade_grip, blade_tip, free_grip);
    const float f = BladeFraction(blade_grip, blade_tip, free_grip);
    if (!(f >= 0.0f) || !(f <= 1.0f)) {
      attached = false;
      return attached;
    }
    const float attach = p.support_attach_m * native_units_per_meter;
    const float blade = p.support_blade_m * native_units_per_meter;
    const float release = p.support_release_m * native_units_per_meter;
    // Mesh, not hitbox: the envelope tightens from the hilt radius to the
    // blade-surface radius along the grab fraction. A hand floating beside
    // the mid-blade (inside the hilt tube but off the mesh) refuses.
    const float allowed = attach + (blade - attach) * f;
    if (!attached) {
      if (d <= allowed) {
        attached = true;
        grab_fraction = f;
      }
    } else if (d > release) {
      attached = false;
    } else {
      grab_fraction = f;
    }
    return attached;
  }
  // Legacy point-proxy overload: handle-only attach. Kept for the existing
  // bench/tests that feed a precomputed distance; new code feeds the segment.
  bool Update(bool grip_held, float hand_to_handle_m,
              const ContactParams& p = {}) {
    if (!grip_held || !(hand_to_handle_m >= 0.0f)) {
      attached = false;
      return attached;
    }
    const float release =
        p.support_release_m >= p.support_attach_m
            ? p.support_release_m
            : p.support_attach_m;
    if (!attached) {
      attached = hand_to_handle_m <= p.support_attach_m;
      if (attached) grab_fraction = 0.0f;
    } else if (hand_to_handle_m > release) {
      attached = false;
    }
    return attached;
  }
};

}  // namespace dsvr
