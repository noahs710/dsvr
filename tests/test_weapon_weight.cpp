// Weapon weight from equip load: bars rise with load, reach never falls.
// No native PARAM read is bound, so the load number is supplied by the caller;
// these checks pin what the scaling may and may not do.
#include "core/weapon_weight_math.h"

#include <cmath>
#include <cstdio>

namespace {
int g_failed = 0;
void Check(bool ok, const char* what) {
  if (!ok) { ++g_failed; std::printf("FAIL: %s\n", what); }
}
}  // namespace

using namespace dsvr;

void PrimeSwing(SwingAttackTrigger& trigger, const SwingAttackParams& params,
                long long& now) {
  Check(!trigger.Feed(0.0f, false, now, params).fired,
        "a stationary sample does not start an attack");
  now += 16;
}
int main() {
  const SwingAttackParams base;  // shipped defaults
  Check(base.slash_speed_min == 2.0f && base.slash_heavy_min == 4.5f,
        "shipped swing bars are the baseline these checks assume");

  // Unknown load changes nothing at all.
  for (float unknown : {-1.0f, -0.0f, -1e9f}) {
    const WeaponWeight w = WeaponWeightFromLoad(unknown);
    Check(!w.reported, "a negative load is not a weight, it is no information");
    const SwingAttackParams p = WeaponWeightSwingParams(base, w);
    Check(p.slash_speed_min == base.slash_speed_min &&
              p.slash_heavy_min == base.slash_heavy_min &&
              p.rest_speed_max == base.rest_speed_max,
            "an unreported load leaves start and stop speeds unchanged");
    Check(WeaponWeightDamageScale(1.0f, w) == 1.0f,
          "an unreported load leaves damage scale alone");
  }
  {
    const WeaponWeight w = WeaponWeightFromLoad(std::nanf(""));
    Check(!w.reported, "a non-finite load is refused, never guessed");
  }

  // The 0..28 span is honoured, and 28 is the top of the scale.
  const WeaponWeight light = WeaponWeightFromLoad(0.0f);
  const WeaponWeight mid = WeaponWeightFromLoad(14.0f);
  const WeaponWeight heavy = WeaponWeightFromLoad(28.0f);
  const WeaponWeight over = WeaponWeightFromLoad(40.0f);
  Check(light.reported && mid.reported && heavy.reported, "real loads are accepted");
  Check(std::fabs(heavy.fraction - 1.0f) < 1e-6f, "28 is full weight");
  Check(std::fabs(over.fraction - 1.0f) < 1e-6f, "a load past 28 clamps, it does not compound");

  const SwingAttackParams pl = WeaponWeightSwingParams(base, light);
  const SwingAttackParams pm = WeaponWeightSwingParams(base, mid);
  const SwingAttackParams ph = WeaponWeightSwingParams(base, heavy);

  Check(pl.slash_speed_min == base.slash_speed_min, "the lightest weapon keeps the light bars");
  Check(pm.slash_speed_min > pl.slash_speed_min &&
            ph.slash_speed_min > pm.slash_speed_min,
        "a heavier weapon needs a more committed swing to register at all");
  Check(pm.slash_heavy_min > pl.slash_heavy_min &&
            ph.slash_heavy_min > pm.slash_heavy_min,
        "a heavier weapon needs a far more committed swing to read as strong");
  Check(ph.slash_heavy_min > base.slash_heavy_min,
        "the strong bar for a heavy weapon sits above the shipped strong bar");

  // Ordering the swing trigger validates for itself: rest < thrust light <= light <= heavy.
  Check(ph.rest_speed_max < ph.slash_speed_min * ph.thrust_ratio,
        "a heavy weapon's rest band stays below its quietest qualifying swing");
  Check(ph.rest_speed_max < ph.slash_speed_min && ph.slash_heavy_min >= ph.slash_speed_min,
        "every scaled bar set keeps its own ordering");
  Check(pl.rest_speed_max == base.rest_speed_max &&
              pm.rest_speed_max == base.rest_speed_max &&
              ph.rest_speed_max == base.rest_speed_max,
        "weapon weight never adds delay or changes the fixed stop-speed band");

  // The reward: one clean heavy hit is worth more than the light flurry.
  const float dl = WeaponWeightDamageScale(1.0f, light);
  const float dm = WeaponWeightDamageScale(1.0f, mid);
  const float dh = WeaponWeightDamageScale(1.0f, heavy);
  Check(dm > dl && dh > dm, "damage scale rises with equip load");
  Check(dh > 1.0f && dh < 3.0f, "the heavy damage bonus is a reward, not a runaway");

  // Slice 1 torque: response gates how fast speed rises, never damage.
  // Unity is transparent, below lags (heavy needs commitment), above leads.
  Check(TorqueGatedSpeed(3.0f, 1.0f) == 3.0f, "unity response is transparent");
  Check(TorqueGatedSpeed(3.0f, 0.5f) < 3.0f, "heavy weapon lags the hand");
  Check(TorqueGatedSpeed(3.0f, 2.0f) > 3.0f, "light weapon leads the hand");
  Check(TorqueGatedSpeed(0.0f, 0.5f) == 0.0f, "no speed from rest, whatever the table");
  Check(TorqueGatedSpeed(3.0f, 0.0f) == 0.0f, "non-positive response refuses");
  Check(TorqueGatedSpeed(3.0f, NAN) == 0.0f, "non-finite response refuses");
  Check(TorqueGatedSpeed(NAN, 1.0f) == 0.0f, "non-finite speed refuses");

  // Reach is never a weight penalty. A longer blade keeps its advantage at
  // every load, and no load pulls the hands in.
  for (float load : {0.0f, 7.0f, 14.0f, 28.0f, 99.0f}) {
    const WeaponWeight w = WeaponWeightFromLoad(load);
    Check(WeaponWeightReach(0.6f, w) == 0.6f, "a short blade keeps its length at any load");
    Check(WeaponWeightReach(1.8f, w) == 1.8f, "a long blade keeps its length at any load");
    Check(WeaponWeightReach(1.8f, w) > WeaponWeightReach(0.6f, w),
          "the longer weapon still out-reaches the shorter one at this load");
  }

  {
    SwingAttackTrigger unprimed;
    bool fired = false;
    for (long long now = 0; now < 640; now += 16)
      fired = unprimed.Feed(8.0f, false, now, ph).fired || fired;
    Check(!fired, "weight scaling never bypasses the mandatory rest gate");
  }

  // The scaled parameters must survive the swing trigger's own validation,
  // or a heavy weapon would silently attack nothing.
  {
    SwingAttackTrigger t;
    long long now = 0;
    bool fired = false;
    PrimeSwing(t, ph, now);
    for (int i = 0; i < 40 && !fired; ++i, now += 16)
      fired = t.Feed(5.5f, false, now, ph).fired;
    Check(fired, "a committed heavy-weapon swing still fires with the scaled bars");
  }
  {
    SwingAttackTrigger t;
    long long now = 0;
    bool fired = false, heavy_fired = false;
    PrimeSwing(t, ph, now);
    // The speed that fires a light weapon's strong attack is only a light
    // attack on a heavy one: the weight has to be paid in speed.
    for (int i = 0; i < 40; ++i, now += 16) {
      const auto out = t.Feed(4.6f, false, now, ph);
      if (out.fired) { fired = true; heavy_fired = out.heavy; }
    }
    SwingAttackTrigger light_t;
    now = 0;
    bool light_fired = false, light_heavy = false;
    PrimeSwing(light_t, pl, now);
    for (int i = 0; i < 40 && !light_fired; ++i, now += 16) {
      const auto o = light_t.Feed(4.6f, false, now, pl);
      if (o.fired) { light_fired = true; light_heavy = o.heavy; }
    }
    Check(fired && !heavy_fired,
          "the same speed qualifies as light, not strong, on a heavy weapon");
    Check(light_fired && light_heavy, "the same speed is a strong attack on a light weapon");
  }

  std::printf(g_failed ? "weapon_weight: %d FAILED\n" : "weapon_weight: all checks passed\n",
              g_failed);
  return g_failed ? 1 : 0;
}
