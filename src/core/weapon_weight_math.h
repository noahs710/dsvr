#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "swing_attack_math.h"
#include "weapon_grip.h"

namespace dsvr {

// Weapon weight from native equip load.
//
// DSR's equip load is an integer-ish stat on a 0..28 scale (28 = the heaviest
// weapon the game carries). Nothing here reads that stat: no PARAM read is
// bound, so the caller supplies the number and says where it came from. The
// math is deliberately independent of that source so a later native read can
// drop in without changing a single threshold rule.
//
// What weight changes, and what it must never change:
//   - The bars a swing must clear go UP with load. A heavy weapon needs a more
//     committed motion to register at all, and a much more committed motion to
//     read as a strong attack, so a controlled heavy strike is the reward.
//   - The stop-speed threshold stays fixed; load adds no timed delay or cooldown.
//   - Damage gain rises with load, so the one clean hit a heavy weapon lands
//     is worth more than the flurry a light one can spam.
//   - Reach NEVER shrinks. A longer weapon keeps its range advantage at every
//     equip load, and nothing in this file limits where the player may put
//     their hands: only real geometry (a wall) does that, and that is E5's job.
//
// Fail-closed: a non-finite load behaves as the lightest weapon (no scaling),
// never as an arbitrary one, and every output keeps the ordering the swing
// trigger's own validation requires.

inline constexpr float kEquipLoadMax = 28.0f;

struct WeaponWeightParams {
  float load_max = kEquipLoadMax;
  float speed_gain = 0.65f;  // +65% swing start bars at full load
  float damage_gain = 0.80f; // +80% native hit scale at full load
};

struct WeaponWeight {
  float load = 0.0f;      // the reported equip load, clamped to [0, load_max]
  float fraction = 0.0f;  // load / load_max, 0..1
  bool reported = false;  // false = no load known, so nothing is scaled
};

// An unknown load (negative, non-finite, or the -1 a profile uses for "not
// recorded") is NOT guessed at: it returns reported=false and every consumer
// leaves its own thresholds alone.
inline WeaponWeight WeaponWeightFromLoad(float load,
                                         const WeaponWeightParams& p = {}) {
  WeaponWeight w;
  if (!std::isfinite(load) || std::signbit(load) || !std::isfinite(p.load_max) ||
      p.load_max <= 0.0f)
    return w;
  w.reported = true;
  w.load = std::min(load, p.load_max);
  w.fraction = w.load / p.load_max;
  return w;
}

// Start-speed bars rise with load. Stop speed remains the trigger's fixed band.
inline SwingAttackParams WeaponWeightSwingParams(
    const SwingAttackParams& base, const WeaponWeight& weight,
    const WeaponWeightParams& p = {}) {
  SwingAttackParams out = base;
  if (!weight.reported) return out;
  const float speed = 1.0f + p.speed_gain * weight.fraction;
  out.slash_speed_min = base.slash_speed_min * speed;
  out.slash_heavy_min = base.slash_heavy_min * speed;
  return out;
}

// Damage scale for one qualified contact. Heavier weapons hit harder per hit;
// the swing bars above are what makes that one hit expensive to earn.
inline float WeaponWeightDamageScale(float base_gain, const WeaponWeight& weight,
                                     const WeaponWeightParams& p = {}) {
  if (!std::isfinite(base_gain) || base_gain < 0.0f || !weight.reported)
    return base_gain;
  return base_gain * (1.0f + p.damage_gain * weight.fraction);
}

// Reach policy. Weight never shortens a weapon and never pulls the player's
// hands in: the blade keeps exactly the length it has, so a physically longer
// weapon keeps its range advantage at any equip load. Kept as a function so
// the invariant is testable rather than implied by absence.
inline float WeaponWeightReach(float blade_length_m, const WeaponWeight&,
                               const WeaponWeightParams& = {}) {
  return blade_length_m;
}

// Slice 1 torque profile: weight shapes how fast tip speed can RISE, not the
// damage it deals. A heavy weapon lags the hand (low response) and needs a
// committed swing to reach the same tip speed a dagger reaches 1:1; the
// energy gate (contact_speed_min -> DamageScaleContinuous) then decides the
// scale. `response` is the per-weapon rate from the offline mass/inertia
// table (torque_response config, 0.2..3.0); <= 0 or non-finite refuses.
// Returns the gated speed the scale should be measured at.
inline float TorqueGatedSpeed(float measured_speed, float response) {
  if (!std::isfinite(measured_speed) || !std::isfinite(response) ||
      response <= 0.0f)
    return 0.0f;
  if (measured_speed <= 0.0f) return 0.0f;
  // response = 1 is transparent; below 1 lags (heavy), above 1 leads (light).
  // Clamped so a mis-tuned table can never manufacture speed from rest.
  const float gated = measured_speed * response;
  if (!std::isfinite(gated) || gated < 0.0f) return 0.0f;
  return gated;
}

// Live-path response resolution: the equipped weapon torque response
// from the offline mass table, falling back to the configured default when
// no equipment is bound or the table has no entry. Fail-closed: unknown
// equipment behaves as the transparent response, never as an arbitrary
// weapon. The caller passes plain IDs so this header stays independent of
// the native equipment snapshot.
inline float SwingWeaponResponse(int weapon_id, bool equipment_bound,
                                    float fallback_response) {
  if (equipment_bound) {
    WeaponMassEntry entry{};
    if (WeaponMassGet(weapon_id, entry)) return entry.response;
  }
  return fallback_response;
}

}  // namespace dsvr
