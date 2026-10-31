#include "../src/core/hand_math.h"

#include <cmath>
#include <cstdio>

int main() {
  int failures = 0;
  auto check = [&](bool ok) { if (!ok) ++failures; };
  auto near = [](float a, float b) { return std::fabs(a - b) < 0.001f; };
  auto vnear = [&](dsvr::Vec3 v, float x, float y, float z) {
    return near(v.x, x) && near(v.y, y) && near(v.z, z);
  };
  using namespace dsvr;

  // Identity grip: blade points along XR forward (-Z), tip one length out.
  check(vnear(BladeDirection(0, 0, 0, 1), 0, 0, -1));
  check(vnear(BladeTip({0, 1.4f, 0}, 0, 0, 0, 1, 0.9f), 0, 1.4f, -0.9f));
  // Yaw +90 deg about Y (quat y=sin45, w=cos45): -Z maps to -X.
  {
    float s = 0.70710678f;
    check(vnear(BladeDirection(0, s, 0, s), -1, 0, 0));
    check(vnear(BladeTip({0, 0, 0}, 0, s, 0, s, 1.0f), -1, 0, 0));
  }
  // Pitch +90 deg about X (blade raised to +Y): -Z maps to +Y.
  {
    float s = 0.70710678f;
    check(vnear(BladeDirection(s, 0, 0, s), 0, 1, 0));
  }
  // Blade direction stays unit for an arbitrary normalised quat.
  {
    Vec3 d = BladeDirection(0.1f, 0.2f, 0.3f, 0.92736185f);
    check(near(VLen(d), 1.0f));
  }

  // Handedness mapping mirrors with the config flag.
  check(WeaponHandIndex(false) == 1 && OffHandIndex(false) == 0);
  check(WeaponHandIndex(true) == 0 && OffHandIndex(true) == 1);

  const Vec3 head{0, 1.65f, 0};
  const Vec3 fwd{0, 0, 1};  // game basis: looking along +Z
  // Held guard in front at chest height blocks; same hand dropped does not.
  check(IsBlocking({0.2f, 1.45f, 0.35f}, 0.2f, head, fwd));
  check(!IsBlocking({0.2f, 0.9f, 0.35f}, 0.2f, head, fwd));
  // Behind the head plane never blocks; a fast held hand is a sweep.
  check(!IsBlocking({0.2f, 1.45f, -0.3f}, 0.2f, head, fwd));
  check(!IsBlocking({0.2f, 1.45f, 0.35f}, 1.5f, head, fwd));
  check(!IsBlocking({NAN, 1.45f, 0.35f}, 0.2f, head, fwd));
  check(!IsBlocking({0.2f, 1.45f, 0.35f}, 0.2f, head, {NAN, 0, 1}));
  check(!IsBlocking({0.2f, 1.45f, 0.35f}, 0.2f, head, {0, 0, 0}));

  // Parry is speed-only at this layer (candidate, never a credit).
  check(IsParryCandidate(2.5f));
  check(!IsParryCandidate(1.0f));
  check(!IsParryCandidate(NAN));

  // Two-hand point proxy retired: the committed blade-segment envelope
  // (SupportGrip, combat_contact.h) replaces the 14 cm grip-point rule.
  // Covered by test_combat_contact segment-envelope checks.

  std::printf("hand math: %d failures\n", failures);
  return failures ? 1 : 0;
}
