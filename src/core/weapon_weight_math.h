#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "swing_attack_math.h"

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
//   - The rest window, settle time and cooldown grow with load: the weapon is
//     slower to bring to bear and slower to recover.
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
  float speed_gain = 0.65f;     // +65% swing bars at full load
  float settle_gain = 1.0f;     // +100% settle time at full load
  float cooldown_gain = 1.25f;  // +125% recovery at full load
  float rest_gain = 0.35f;      // rest band rises too, or a heavy arm at rest
                                // reads as a slow swing
  float damage_gain = 0.80f;    // +80% damage scale at full load
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

// Bars rise with load, and the ordering the swing trigger validates stays
// intact: rest < thrust light bar <= light bar <= heavy bar.
inline SwingAttackParams WeaponWeightSwingParams(
    const SwingAttackParams& base, const WeaponWeight& weight,
    const WeaponWeightParams& p = {}) {
  SwingAttackParams out = base;
  if (!weight.reported) return out;
  const float speed = 1.0f + p.speed_gain * weight.fraction;
  const float rest = 1.0f + p.rest_gain * weight.fraction;
  out.slash_speed_min = base.slash_speed_min * speed;
  out.slash_heavy_min = base.slash_heavy_min * speed;
  out.rest_speed_max = base.rest_speed_max * rest;
  // A heavy weapon's rest band must stay under its own quietest possible
  // qualifying swing, or the detector would call a slow drag a rest and a
  // slow drag a swing at the same time.
  out.rest_speed_max =
      std::min(out.rest_speed_max, out.slash_speed_min * out.thrust_ratio * 0.5f);
  const float settle = 1.0f + p.settle_gain * weight.fraction;
  out.settle_ms = base.settle_ms + long long(base.settle_ms * (settle - 1.0f));
  out.rest_ms_min =
      base.rest_ms_min + long long(base.rest_ms_min * (settle - 1.0f));
  out.cooldown_ms =
      base.cooldown_ms +
      long long(base.cooldown_ms * p.cooldown_gain * weight.fraction);
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

}  // namespace dsvr
