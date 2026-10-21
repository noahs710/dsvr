// Unit tests for headlook math (no game needed). CHECK, never assert:
// assert vanishes under NDEBUG and would silently skip the tested calls.
#include <cmath>
#include <cstdio>

#include "../src/core/camera_math.h"

static int g_failures = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      ++g_failures;                                                     \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
    }                                                                   \
  } while (0)

using namespace dsvr;

static bool Near(float a, float b, float eps = 1e-4f) {
  return std::fabs(a - b) < eps;
}
static bool VNear(Vec3 v, float x, float y, float z) {
  return Near(v.x, x) && Near(v.y, y) && Near(v.z, z);
}
// Build XR quaternion for rotation about Y by angle (rad).
static void QuatAxisAngle(float ax, float ay, float az, float ang,
                          float q[4]) {
  float s = std::sin(ang * 0.5f), c = std::cos(ang * 0.5f);
  q[0] = ax * s; q[1] = ay * s; q[2] = az * s; q[3] = c;
}

int main() {
  const float kPi = 3.14159265f;

  CHECK(VNear(VMid({-0.032f, 1.6f, 0.0f}, {0.032f, 1.6f, 0.0f}),
              0.0f, 1.6f, 0.0f));
  Vec3 planar = VPlanarForward({0.0f, 0.8f, -0.6f});
  CHECK(VNear(planar, 0.0f, 0.0f, -1.0f));
  Vec3 lean = HeadTranslationWorld({0.1f, 0.2f, -0.3f},
                                   {1, 0, 0}, {0, 1, 0}, {0, 0, -1}, 2.0f);
  CHECK(VNear(lean, 0.2f, 0.4f, -0.6f));

  // Identity delta -> zero yaw/pitch.
  {
    float q0[4] = {0, 0, 0, 1}, q[4] = {0, 0, 0, 1};
    auto yp = YawPitchFromXrDelta(q0, q);
    CHECK(Near(yp.yaw, 0) && Near(yp.pitch, 0));
  }
  // Yaw +90 deg about Y in XR: forward -Z -> -X... atan2 formula check.
  {
    float q0[4] = {0, 0, 0, 1}, q[4];
    QuatAxisAngle(0, 1, 0, kPi / 2, q);
    auto yp = YawPitchFromXrDelta(q0, q);
    CHECK(Near(std::fabs(yp.yaw), kPi / 2, 1e-3f));
    CHECK(Near(yp.pitch, 0, 1e-3f));
  }
  // Pitch: rotate about X by +30 deg tips XR forward up (f.y > 0), since
  // XR forward is -Z (right-hand rule about +X sends -Z toward +Y).
  {
    float q0[4] = {0, 0, 0, 1}, q[4];
    QuatAxisAngle(1, 0, 0, kPi / 6, q);
    auto yp = YawPitchFromXrDelta(q0, q);
    CHECK(Near(yp.pitch, kPi / 6, 1e-3f));
  }
  // Non-identity reference: same orientation as reference -> zero delta.
  {
    float q0[4], q[4];
    QuatAxisAngle(0, 1, 0, 0.7f, q0);
    QuatAxisAngle(0, 1, 0, 0.7f, q);
    auto yp = YawPitchFromXrDelta(q0, q);
    CHECK(Near(yp.yaw, 0, 1e-3f) && Near(yp.pitch, 0, 1e-3f));
  }
  // ApplyYawPitch with the game's own basis orientation (right=X, up=Y,
  // fwd=+Z, left-handed as in the title-camera evidence): zero angles
  // returns the basis unchanged. The old GL-style basis (fwd=-Z) let a
  // mirrored basis (det -1) pass; this one cannot.
  {
    Vec3 r, u, f;
    ApplyYawPitch({1, 0, 0}, {0, 1, 0}, {0, 0, 1}, 0, 0, r, u, f);
    CHECK(VNear(r, 1, 0, 0) && VNear(u, 0, 1, 0) && VNear(f, 0, 0, 1));
  }
  // Result stays orthonormal AND left-handed (r.(u x f) = +1) for a
  // mixed rotation. A mirrored basis gives -1 and culls floor planes.
  {
    Vec3 r, u, f;
    ApplyYawPitch({1, 0, 0}, {0, 1, 0}, {0, 0, 1}, 0.6f, -0.35f, r, u, f);
    CHECK(Near(VLen(r), 1) && Near(VLen(u), 1) && Near(VLen(f), 1));
    CHECK(Near(VDot(r, u), 0, 1e-3f) && Near(VDot(r, f), 0, 1e-3f));
    CHECK(Near(VDot(u, f), 0, 1e-3f));
    CHECK(Near(VDot(r, VCross(u, f)), 1, 1e-3f));
  }
  // Pitch up (positive) raises forward's Y for a level camera.
  {
    Vec3 r, u, f;
    ApplyYawPitch({1, 0, 0}, {0, 1, 0}, {0, 0, 1}, 0, kPi / 6, r, u, f);
    CHECK(Near(f.y, 0.5f, 1e-3f));
  }
  // Head-right is negative yaw (YawPitchFromXrDelta); it must swing
  // forward toward game right (+X here), not away from it.
  {
    Vec3 r, u, f;
    ApplyYawPitch({1, 0, 0}, {0, 1, 0}, {0, 0, 1}, -kPi / 6, 0, r, u, f);
    CHECK(Near(f.x, 0.5f, 1e-3f) && Near(f.z, 0.8660254f, 1e-3f));
  }
  // RebaseVector: same basis is identity; rotated basis follows.
  {
    Vec3 v = RebaseVector({0, 0, -1}, {1, 0, 0}, {0, 1, 0}, {0, 0, -1},
                          {1, 0, 0}, {0, 1, 0}, {0, 0, -1});
    CHECK(VNear(v, 0, 0, -1));
    // Game turned so its forward is now +X (right=-Z): forward follows.
    Vec3 w = RebaseVector({0, 0, -1}, {1, 0, 0}, {0, 1, 0}, {0, 0, -1},
                          {0, 0, -1}, {0, 1, 0}, {1, 0, 0});
    CHECK(VNear(w, 1, 0, 0));
  }
  // Chained rebase: game basis yawing 1 deg/frame for 90 frames must
  // carry the captured baseline the full 90 deg (stick-turn following).
  {
    Vec3 br{1, 0, 0}, bu{0, 1, 0}, bf{0, 0, -1};
    Vec3 pr = br, pu = bu, pf = bf;
    for (int i = 0; i < 90; ++i) {
      float a = kPi / 180.0f;
      Vec3 cr = VNorm(VRotate(pr, {0, 1, 0}, a));
      Vec3 cf = VNorm(VRotate(pf, {0, 1, 0}, a));
      Vec3 cu = pu;  // game basis arrives renormalised each frame
      br = RebaseVector(br, pr, pu, pf, cr, cu, cf);
      bu = RebaseVector(bu, pr, pu, pf, cr, cu, cf);
      bf = RebaseVector(bf, pr, pu, pf, cr, cu, cf);
      pr = cr; pu = cu; pf = cf;
    }
    // Forward started -Z; yawing +90 deg about Y (right-hand) sends it to -X.
    CHECK(VNear(bf, -1, 0, 0));
    CHECK(Near(VLen(bf), 1, 1e-3f));
  }
  // Tangent placement: for mirror-asymmetric eyes (Quest-like frustums),
  // the rect spanning tangents [-1,+1] must be centred on tangent 0 in
  // BOTH eyes -- otherwise the eyes cannot fuse the image.
  {
    float tan_h = 1.0f;
    // Left eye: angleLeft -0.942, angleRight 0.698 (live-logged values).
    float ll = std::tan(-0.942f), lr = std::tan(0.698f);
    float lx0 = TangentToFraction(-tan_h, ll, lr);
    float lx1 = TangentToFraction(tan_h, ll, lr);
    // Right eye is the mirror.
    float rx0 = TangentToFraction(-tan_h, -lr, -ll);
    float rx1 = TangentToFraction(tan_h, -lr, -ll);
    auto centre_tangent = [](float x0, float x1, float tmin, float tmax) {
      float frac = (x0 + x1) * 0.5f;
      return tmin + frac * (tmax - tmin);
    };
    CHECK(Near(centre_tangent(lx0, lx1, ll, lr), 0.0f, 1e-4f));
    CHECK(Near(centre_tangent(rx0, rx1, -lr, -ll), 0.0f, 1e-4f));
    // And the old full-eye mapping (fractions 0..1 in both eyes) is NOT
    // centred: midpoint tangent = (tl+tr)/2 != 0 per eye, mirrored.
    CHECK(std::fabs((ll + lr) * 0.5f) > 0.2f);
  }
  // QuatRotate sanity: +90 deg about Y sends -Z to -X (right-hand rule).
  {
    float q[4];
    QuatAxisAngle(0, 1, 0, kPi / 2, q);
    Vec3 f = QuatRotate({0, 0, -1}, q[0], q[1], q[2], q[3]);
    CHECK(VNear(f, -1, 0, 0));
  }

  // Stable extraction: looking near-straight down (pitch about X by
  // -80 deg in XR) must hold the fallback yaw instead of spinning, and
  // pitch clamps at 89 deg.
  {
    float q0[4] = {0, 0, 0, 1}, q[4];
    QuatAxisAngle(1, 0, 0, -1.3962634f, q);  // -80 deg
    auto yp = YawPitchFromXrDeltaStable(q0, q, 0.77f);
    CHECK(Near(yp.yaw, 0.77f, 1e-3f));
    CHECK(Near(yp.pitch, -1.3962634f, 1e-3f));
    float q2[4];
    QuatAxisAngle(0, 1, 0, 0.5f, q2);
    auto yp2 = YawPitchFromXrDeltaStable(q0, q2, 0.77f);
    CHECK(Near(std::fabs(yp2.yaw), 0.5f, 1e-3f));  // normal yaw still tracks
  }
  // PlanarYawDelta matches the RotateBasisAboutUp convention.
  {
    CHECK(Near(PlanarYawDelta({0, 0, 1}, {0.70710678f, 0, 0.70710678f}),
               kPi / 4, 1e-3f));
    CHECK(Near(PlanarYawDelta({0, 0, 1}, {-0.70710678f, 0, 0.70710678f}),
               -kPi / 4, 1e-3f));
    CHECK(Near(PlanarYawDelta({0, 1, 0}, {0, 0, 1}), 0.0f, 1e-6f));
    CHECK(Near(PlanarYawDelta({0, 0, -1}, {0, 0, -1}), 0.0f, 1e-6f));
  }

  // BasisFromXrDelta: identity returns the body basis (game LH basis).
  {
    float q0[4] = {0, 0, 0, 1}, q[4] = {0, 0, 0, 1};
    Basis b = BasisFromXrDelta(q0, q, {1, 0, 0}, {0, 1, 0}, {0, 0, 1},
                               {1, 0, 0});
    CHECK(VNear(b.f, 0, 0, 1) && VNear(b.r, 1, 0, 0) && VNear(b.u, 0, 1, 0));
    CHECK(Near(VDot(b.r, VCross(b.u, b.f)), 1, 1e-3f));  // left-handed
  }
  // Pitch down 80 deg: forward dives, right stays the body right (this is
  // the case that spun the Euler version), horizon stays level.
  {
    float q0[4] = {0, 0, 0, 1}, q[4];
    QuatAxisAngle(1, 0, 0, -1.3962634f, q);
    Basis b = BasisFromXrDelta(q0, q, {1, 0, 0}, {0, 1, 0}, {0, 0, 1},
                               {1, 0, 0});
    CHECK(Near(b.f.y, -0.98480775f, 1e-3f));
    CHECK(VNear(b.r, 1, 0, 0));
    CHECK(Near(b.u.y, 0.17364818f, 1e-3f));
  }
  // Agrees with ApplyYawPitch for a moderate yaw+pitch (same forward).
  {
    float q0[4] = {0, 0, 0, 1};
    float qy[4], qp[4], q[4];
    QuatAxisAngle(0, 1, 0, -0.5f, qy);   // head right (XR yaw sign as live)
    QuatAxisAngle(1, 0, 0, -0.3f, qp);   // pitch down
    QuatMul(qy, qp, q);
    Basis b = BasisFromXrDelta(q0, q, {1, 0, 0}, {0, 1, 0}, {0, 0, 1},
                               {1, 0, 0});
    auto yp = YawPitchFromXrDelta(q0, q);
    Vec3 r2, u2, f2;
    ApplyYawPitch({1, 0, 0}, {0, 1, 0}, {0, 0, 1}, yp.yaw, yp.pitch, r2, u2,
                  f2);
    CHECK(VNear(b.f, f2.x, f2.y, f2.z));
  }

  // Anchor source: logic pos wins when valid (immediate); physics is the
  // fallback. Walking on stale physics pos lagged the camera behind the
  // model (Havok trails 0.2-0.7 s; headset report 2026-10-13).
  CHECK(VNear(SelectAnchorPos({1, 2, 3}, true, {9, 9, 9}), 1, 2, 3));
  CHECK(VNear(SelectAnchorPos({1, 2, 3}, false, {9, 9, 9}), 9, 9, 9));

  // A nonzero reference must cancel exactly; the old delta-then-reference
  // transform applied calibration twice and turned even at capture.
  {
    float q0[4];
    QuatAxisAngle(0, 1, 0, 1.1f, q0);
    Basis b = BasisFromXrDelta(q0, q0, {1, 0, 0}, {0, 1, 0}, {0, 0, 1},
                               {1, 0, 0});
    CHECK(VNear(b.f, 0, 0, 1));
    CHECK(VNear(b.r, 1, 0, 0));
  }
  // Native head roll follows tracking rather than rolling the world with
  // a level camera. Reflection into D3D preserves handedness.
  {
    float q0[4] = {0, 0, 0, 1}, q[4];
    QuatAxisAngle(0, 0, 1, kPi / 6, q);
    Basis b = BasisFromXrDelta(q0, q, {1, 0, 0}, {0, 1, 0}, {0, 0, 1},
                               {1, 0, 0});
    CHECK(VNear(b.r, 0.8660254f, 0.5f, 0));
    CHECK(Near(VDot(b.r, VCross(b.u, b.f)), 1));
  }

  if (g_failures == 0) std::printf("test_camera_math: all checks passed\n");
  return g_failures ? 1 : 0;
}
