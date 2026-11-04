// Inertial weapon lag stepper (lane B bench follow-up). No game, no XR:
// pure math on DSVR quat/vec types. ERVR-derived design, reimplemented
// (see weapon_inertia_math.h header note).
#include <cmath>
#include <cstdio>

#include "../src/core/weapon_inertia_math.h"

#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)

namespace {

float Separation(dsvr::AimQuat a, dsvr::AimQuat b) {
  return dsvr::VLen(dsvr::AimQuatRotationVector(
      dsvr::AimQuatNorm(dsvr::AimQuatMul(dsvr::AimQuatConj(a), b))));
}

bool Unit(dsvr::AimQuat q) {
  const float n = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
  return std::fabs(n - 1.f) < 1e-4f;
}

// 30-degree pitch target from identity: under the 45-degree held-lag clamp
// so light-vs-heavy ordering is visible instead of both sitting at it.
// Pitch, not yaw: the shaft is Y, and twist about the shaft is removed, so
// a yaw would be eaten as twist and both weights would snap to the target.
const dsvr::AimQuat kTarget{0.25881905f, 0.0f, 0.0f, 0.96592583f};
const dsvr::Vec3 kShaft{0, 1, 0};

dsvr::AimQuat StepN(dsvr::AimQuat out, int n, float weight) {
  for (int i = 0; i < n; ++i)
    out = dsvr::StepWeaponLag(out, kTarget, kShaft, 1.f / 90.f, weight, 1.0f,
                              0.3f, 1.0f, 0.3f, 6.0f, 30.0f, 1.0f, 8.0f,
                              0.7853982f);
  return out;
}

}  // namespace

int main() {
  using namespace dsvr;
  const AimQuat kIdentity{0, 0, 0, 1};
  // ---- degenerate inputs return the target: tracking always wins ----
  CHECK(Separation(StepWeaponLag(kIdentity, kTarget, kShaft, 1.f / 90.f, 0.0f,
                                 1.0f, 0.3f, 1.0f, 0.3f, 6.0f, 30.0f, 1.0f,
                                 8.0f, 0.7853982f),
                   kTarget) < 1e-6f);
  CHECK(Separation(StepWeaponLag(kIdentity, kTarget, kShaft, 0.0f, 1.0f, 1.0f,
                                 0.3f, 1.0f, 0.3f, 6.0f, 30.0f, 1.0f, 8.0f,
                                 0.7853982f),
                   kTarget) < 1e-6f);
  // ---- heavy lags light; both converge monotonically ----
  const AimQuat light = StepN(kIdentity, 8, 0.2f);
  const AimQuat heavy = StepN(kIdentity, 8, 3.0f);
  CHECK(Unit(light) && Unit(heavy));
  const float d_light = Separation(light, kTarget);
  const float d_heavy = Separation(heavy, kTarget);
  CHECK(d_heavy > d_light && d_light < 1.5707963f);
  float prev = 1.5707963f;
  AimQuat out = kIdentity;
  for (int i = 0; i < 30; ++i) {
    out = StepWeaponLag(out, kTarget, kShaft, 1.f / 90.f, 1.0f, 1.0f, 0.3f,
                        1.0f, 0.3f, 6.0f, 30.0f, 1.0f, 8.0f, 0.7853982f);
    const float d = Separation(out, kTarget);
    CHECK(d <= prev + 1e-6f);  // monotone: no fling past the hand
    prev = d;
  }
  CHECK(Unit(out));
  const AimQuat settled = StepN(kIdentity, 300, 0.2f);
  CHECK(Separation(settled, kTarget) < 0.02f);  // light converges ~fully
  // ---- huge separation is clamped to the 45-degree held lag ----
  const AimQuat far{0.9961947f, 0.0f, 0.0f, 0.0871557f};  // ~170 deg pitch
  const AimQuat first =
      StepWeaponLag(far, kTarget, kShaft, 1.f / 90.f, 3.0f, 1.0f, 0.3f, 1.0f,
                    0.3f, 6.0f, 30.0f, 1.0f, 8.0f, 0.7853982f);
  CHECK(Unit(first) && Separation(first, kTarget) < 0.80f);
  std::puts("test_weapon_inertia: PASS");
  return 0;
}
