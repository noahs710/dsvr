#pragma once
#include <cmath>

namespace dsvr {

// Pure, unit-tested math for headlook / first-person camera work.
// Game basis convention (verified from CameraMan rows): orthonormal
// right/up/forward rows, forward = where the camera looks.

struct Vec3 {
  float x, y, z;
};

inline Vec3 VAdd(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 VScale(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float VDot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 VCross(Vec3 a, Vec3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
          a.x * b.y - a.y * b.x};
}
inline float VLen(Vec3 a) { return std::sqrt(VDot(a, a)); }
inline Vec3 VNorm(Vec3 a) {
  float l = VLen(a);
  return l > 1e-6f ? VScale(a, 1.0f / l) : Vec3{0, 0, 0};
}
inline Vec3 VMid(Vec3 a, Vec3 b) {
  return {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f,
          (a.z + b.z) * 0.5f};
}
inline Vec3 VPlanarForward(Vec3 f) {
  Vec3 p{f.x, 0.0f, f.z};
  return VLen(p) > 0.0001f ? VNorm(p) : Vec3{0.0f, 0.0f, -1.0f};
}
// Anchor-source policy (pure, tested): prefer a readable ChrPosData position.
// This selection does not prove which source is coherent with the rendered
// model. The walking-lag retest failed; temporal ownership needs a trace.
inline Vec3 SelectAnchorPos(Vec3 logic_pos, bool logic_ok, Vec3 physics_pos) {
  return logic_ok ? logic_pos : physics_pos;
}
inline Vec3 HeadTranslationWorld(Vec3 local_delta, Vec3 body_right,
                                 Vec3 body_up, Vec3 body_forward,
                                 float world_scale) {
  return VAdd(VAdd(VScale(body_right, local_delta.x * world_scale),
                   VScale(body_up, local_delta.y * world_scale)),
              VScale(body_forward, -local_delta.z * world_scale));
}
inline Vec3 VRotate(Vec3 v, Vec3 axis, float angle) {
  float c = std::cos(angle), s = std::sin(angle);
  return VAdd(VAdd(VScale(v, c), VScale(VCross(axis, v), s)),
              VScale(axis, VDot(axis, v) * (1.0f - c)));
}

// Rotate vector by quaternion (x,y,z,w).
inline Vec3 QuatRotate(Vec3 v, float qx, float qy, float qz, float qw) {
  Vec3 q{qx, qy, qz};
  Vec3 t = VScale(VCross(q, v), 2.0f);
  return VAdd(v, VAdd(VScale(t, qw), VCross(q, t)));
}
// Recenter heading only: pitch/roll remain physical tracking, and the
// arbitrary XR LOCAL-space heading is shared by camera and arms.
inline void CaptureYawReference(const float q[4], float out[4]) {
  Vec3 f = QuatRotate({0, 0, -1}, q[0], q[1], q[2], q[3]);
  float yaw = std::atan2(-f.x, -f.z);
  out[0] = 0; out[1] = std::sin(yaw * 0.5f);
  out[2] = 0; out[3] = std::cos(yaw * 0.5f);
}
// Transform tracking-space vectors through the same reference as orientation.
inline Vec3 HeadTranslationFromReference(Vec3 delta, const float q0[4],
                                          Vec3 r, Vec3 u, Vec3 f, float scale) {
  Vec3 local = QuatRotate(delta, -q0[0], -q0[1], -q0[2], q0[3]);
  return HeadTranslationWorld(local, r, u, f, scale);
}
// Quaternion product a (x) b (apply b, then a), xyzw components.
inline void QuatMul(float a[4], float b[4], float out[4]) {
  out[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
  out[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
  out[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
  out[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}

// Yaw (about +Y) and pitch extracted from an XR-space orientation delta:
// XR forward is -Z, up +Y. Looking right = negative yaw by this formula;
// callers apply with the matching sign (unit-tested).
struct YawPitch {
  float yaw, pitch;
};
inline YawPitch YawPitchFromXrDelta(float q0[4], float q[4]) {
  float conj[4] = {-q0[0], -q0[1], -q0[2], q0[3]};
  float d[4];
  QuatMul(q, conj, d);
  Vec3 f = QuatRotate({0, 0, -1}, d[0], d[1], d[2], d[3]);
  float cy = f.y < -1 ? -1 : (f.y > 1 ? 1 : f.y);
  return {std::atan2(-f.x, -f.z), std::asin(cy)};
}

// Yaw/pitch with continuity near the poles. Looking straight up or down
// makes yaw mathematically undefined (the planar forward vanishes), and
// the plain extraction spins wildly there -- seen live as yaw jumping
// +-170 deg while pitch sat near -80, i.e. visible jank every time the
// user looked at their feet. Near a pole we hold the last stable yaw;
// pitch is clamped just shy of +-90 so the basis never flips.
inline YawPitch YawPitchFromXrDeltaStable(float q0[4], float q[4],
                                          float fallback_yaw) {
  float conj[4] = {-q0[0], -q0[1], -q0[2], q0[3]};
  float d[4];
  QuatMul(q, conj, d);
  Vec3 f = QuatRotate({0, 0, -1}, d[0], d[1], d[2], d[3]);
  float planar = std::sqrt(f.x * f.x + f.z * f.z);
  float yaw = planar > 0.2f ? std::atan2(-f.x, -f.z) : fallback_yaw;
  float cy = f.y < -1 ? -1 : (f.y > 1 ? 1 : f.y);
  float pitch = std::asin(cy);
  const float kMaxPitch = 1.5533f;  // 89 degrees
  if (pitch > kMaxPitch) pitch = kMaxPitch;
  if (pitch < -kMaxPitch) pitch = -kMaxPitch;
  return {yaw, pitch};
}

// Signed planar (XZ) angle from forward `a` to forward `b`, in the
// RotateBasisAboutUp convention: positive rotates (0,0,1) toward +X.
// Near-vertical inputs return 0 (no measurable planar turn).
inline float PlanarYawDelta(Vec3 a, Vec3 b) {
  Vec3 pa{a.x, 0.0f, a.z}, pb{b.x, 0.0f, b.z};
  if (VLen(pa) < 1e-4f || VLen(pb) < 1e-4f) return 0.0f;
  pa = VNorm(pa);
  pb = VNorm(pb);
  float cross_y = pa.z * pb.x - pa.x * pb.z;  // (a x b) . +Y... sign below
  // a=(0,0,1) -> b=(sin,0,cos) must yield +angle; (a x b).y = a.z*b.x -
  // a.x*b.z = sin, so atan2(cross_y, dot) matches RotateBasisAboutUp.
  return std::atan2(cross_y, VDot(pa, pb));
}

// Full tracked orientation, reflected from XR (-Z forward) into game (+Z).
// Use inverse(reference) * current exactly once. Mapping both forward and
// right preserves physical roll and avoids an Euler pole at vertical look.
struct Basis {
  Vec3 r, u, f;
};
inline Basis BasisFromXrDelta(float q0[4], float q[4], Vec3 body_r,
                              Vec3 body_u, Vec3 body_f, Vec3 prev_right) {
  (void)prev_right;  // kept for the existing interface
  float conj[4] = {-q0[0], -q0[1], -q0[2], q0[3]}, d[4];
  QuatMul(conj, q, d);
  auto map = [&](Vec3 v) {
    return HeadTranslationWorld(v, body_r, body_u, body_f, 1.0f);
  };
  Vec3 f = VNorm(map(QuatRotate({0, 0, -1}, d[0], d[1], d[2], d[3])));
  Vec3 r = VNorm(map(QuatRotate({1, 0, 0}, d[0], d[1], d[2], d[3])));
  Vec3 u = VNorm(VCross(f, r));
  r = VNorm(VCross(u, f));
  return {r, u, f};
}

// Apply yaw/pitch to a captured game basis.
// yaw: about captured up. pitch: about yawed right.
//
// GAME BASIS IS LEFT-HANDED: rows satisfy r = u x f and u = f x r (title
// camera at (0,4,-8) looks along +Z with right = +X; D3D11 convention).
// The first version built r = f x u, which mirrors the basis (det -1).
// In the 0.9.2 in-game test (2026-10-13) that produced a mirrored image
// (both look axes felt inverted) and flipped face winding, which culled
// floor planes while walls still rendered. Pitch had the same LH sign
// error: positive pitch (head up) wrote fwd.y < 0 in every logged
// sample. Both are fixed below; test_camera_math now uses the game's
// own basis orientation (fwd = +Z) instead of the GL-style (fwd = -Z)
// basis the original draft was verified against.
inline void ApplyYawPitch(Vec3 right0, Vec3 up0, Vec3 fwd0, float yaw,
                          float pitch, Vec3& right_out, Vec3& up_out,
                          Vec3& fwd_out) {
  Vec3 f1 = VRotate(fwd0, up0, -yaw);
  Vec3 r1 = VRotate(right0, up0, -yaw);
  Vec3 f2 = VNorm(VRotate(f1, r1, -pitch));
  Vec3 r2 = VNorm(VCross(up0, f2));
  Vec3 u2 = VNorm(VCross(f2, r2));
  right_out = r2;
  up_out = u2;
  fwd_out = f2;
}

// Map an angle tangent to a [0,1] fraction across a frustum's tangent
// range [tmin, tmax]. Used to place the rendered image at angularly
// identical positions in both (mirror-asymmetric) eyes so they fuse.
inline float TangentToFraction(float t, float tmin, float tmax) {
  return (t - tmin) / (tmax - tmin);
}

// Rotate a captured body basis about its own up axis by `angle` radians
// (positive = turn right: forward gains right-vector component). Used for
// XR stick turning (turn_math.h deltas): the turn rotates the BODY frame
// the HMD delta is measured against, never the head pose itself, so lean
// translation stays in the turned frame. Up is unchanged by construction.
// Sign pinned by tests on the game basis (fwd = +Z, right = +X): a +45 deg
// turn maps fwd (0,0,1) to (sin45, 0, cos45).
inline void RotateBasisAboutUp(Vec3 right0, Vec3 up0, Vec3 fwd0, float angle,
                               Vec3& right_out, Vec3& fwd_out) {
  right_out = VNorm(VRotate(right0, up0, angle));
  fwd_out = VNorm(VRotate(fwd0, up0, angle));
}

// Express `v` (given in last frame's basis) in the game's current basis:
// used to follow stick-turns of the underlying game camera without
// re-capturing the HMD reference.
inline Vec3 RebaseVector(Vec3 v, Vec3 r_last, Vec3 u_last, Vec3 f_last,
                         Vec3 r_cur, Vec3 u_cur, Vec3 f_cur) {
  return VAdd(VAdd(VScale(r_cur, VDot(v, r_last)),
                   VScale(u_cur, VDot(v, u_last))),
              VScale(f_cur, VDot(v, f_last)));
}

}  // namespace dsvr
