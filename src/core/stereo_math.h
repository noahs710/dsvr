#pragma once
#include <algorithm>
#include <cmath>
#include <limits>

#include "camera_math.h"

namespace dsvr {

// Stereo geometry. Runtime integration separately owns pass selection, XR to
// native pose mapping, render scheduling and target lifetime. NativeEyeClip
// builds real asymmetric projections; the symmetric-frustum/sub-rect helpers
// below also support the existing mono presentation path.
// Tangent conventions match xr.cpp exactly: tan(angleLeft) is negative,
// tan(angleRight) positive; aspect = (tr - tl) / (tu - td). All numbers
// come from the runtime (xrLocateViews), never from a headset spec.

struct EyeFrustum {
  float tan_left = -1.0f;   // tan(angleLeft), negative
  float tan_right = 1.0f;   // tan(angleRight), positive
  float tan_up = 1.0f;      // tan(angleUp), positive
  float tan_down = -1.0f;   // tan(angleDown), negative
};

inline EyeFrustum FrustumFromAngles(float angle_left, float angle_right,
                                    float angle_up, float angle_down) {
  return {std::tan(angle_left), std::tan(angle_right), std::tan(angle_up),
          std::tan(angle_down)};
}

inline float FrustumVerticalFov(const EyeFrustum& f) {
  return std::atan(f.tan_up) - std::atan(f.tan_down);
}
inline float FrustumAspect(const EyeFrustum& f) {
  float h = f.tan_up - f.tan_down;
  return h > 1e-6f ? (f.tan_right - f.tan_left) / h : 0.0f;
}

// Mono theater screen extents. The user controls horizontal FOV; derive the
// vertical tangent from the game's frustum aspect so the source image keeps
// its intended shape instead of stretching to the same angle in both axes.
struct ScreenTangentExtents {
  float tan_half_h = 0.0f;
  float tan_half_v = 0.0f;
};
inline ScreenTangentExtents TheaterScreenExtents(float horizontal_fov_rad,
                                                  float aspect) {
  if (!std::isfinite(horizontal_fov_rad) || !std::isfinite(aspect) ||
      horizontal_fov_rad <= 0.0f || horizontal_fov_rad >= 3.14159265f ||
      aspect <= 1e-6f)
    return {};
  const float tan_half_h = std::tan(horizontal_fov_rad * 0.5f);
  const float tan_half_v = tan_half_h / aspect;
  if (!std::isfinite(tan_half_h) || !std::isfinite(tan_half_v) ||
      tan_half_h <= 0.0f || tan_half_v <= 0.0f)
    return {};
  return {tan_half_h, tan_half_v};
}

// Clamp a virtual theater screen to the common visible angular area of both
// eyes. A user can request a wider screen than their headset sees; cropping
// the outer part clips HUD text. Keep the requested aspect while shrinking
// just enough to fit both asymmetric eye frusta.
inline ScreenTangentExtents FitTheaterScreenToEyes(
    ScreenTangentExtents requested, const EyeFrustum eyes[2], float aspect) {
  if (!eyes || !std::isfinite(requested.tan_half_h) ||
      !std::isfinite(requested.tan_half_v) || !std::isfinite(aspect) ||
      requested.tan_half_h <= 0.0f || requested.tan_half_v <= 0.0f ||
      aspect <= 1e-6f)
    return {};
  float max_half_h = (std::numeric_limits<float>::max)();
  float max_half_v = (std::numeric_limits<float>::max)();
  for (unsigned i = 0; i < 2; ++i) {
    const EyeFrustum& eye = eyes[i];
    const float values[] = {eye.tan_left, eye.tan_right, eye.tan_up, eye.tan_down};
    for (float value : values) if (!std::isfinite(value)) return {};
    const float half_h = std::min(-eye.tan_left, eye.tan_right);
    const float half_v = std::min(eye.tan_up, -eye.tan_down);
    if (half_h <= 0.0f || half_v <= 0.0f) return {};
    max_half_h = std::min(max_half_h, half_h);
    max_half_v = std::min(max_half_v, half_v);
  }
  const float tan_h = std::min(requested.tan_half_h,
                               std::min(max_half_h, max_half_v * aspect));
  if (!(tan_h > 0.0f) || !std::isfinite(tan_h)) return {};
  return {tan_h, tan_h / aspect};
}


// The game's single symmetric frustum that covers an eye: vertical FOV of
// the widest eye (xr.cpp policy -- neither eye may be asked to show more
// than the game rendered), expressed as symmetric tangents.
struct SymmetricFrustum {
  float tan_half_h = 0.0f;  // horizontal tangent magnitude
  float tan_half_v = 0.0f;  // vertical tangent magnitude
  float vfov_rad = 0.0f;
  float aspect = 0.0f;
};

inline SymmetricFrustum GameFrustumFor(const EyeFrustum& widest_eye) {
  SymmetricFrustum out;
  out.vfov_rad = FrustumVerticalFov(widest_eye);
  out.aspect = FrustumAspect(widest_eye);
  out.tan_half_v = std::tan(out.vfov_rad * 0.5f);
  out.tan_half_h = out.tan_half_v * out.aspect;
  return out;
}

// Sub-rect (fractions of the eye buffer, x right, y DOWN from the top row)
// where the symmetric game image lands inside one asymmetric eye, so both
// eyes fuse. Generalises the immersive-rect math inlined in xr.cpp; the
// mono path and the H3 hook must share this one tested implementation.
struct EyeSubRect {
  float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;  // fractions, 0..1
};

inline EyeSubRect SubRectForEye(const SymmetricFrustum& game,
                                const EyeFrustum& eye) {
  EyeSubRect out;
  float x0 = TangentToFraction(-game.tan_half_h, eye.tan_left, eye.tan_right);
  float x1 = TangentToFraction(game.tan_half_h, eye.tan_left, eye.tan_right);
  // Fractions run from angleDown; pixel rows run from angleUp (top).
  float lo = TangentToFraction(-game.tan_half_v, eye.tan_down, eye.tan_up);
  float hi = TangentToFraction(game.tan_half_v, eye.tan_down, eye.tan_up);
  out.x = x0;
  out.y = 1.0f - hi;
  out.w = x1 - x0;
  out.h = hi - lo;
  return out;
}

// Eye-anchor helpers for the first-person camera: xrLocateViews gives each
// eye's pose in LOCAL space; the FP anchor uses their midpoint (xr.cpp
// policy -- yaw must not orbit the head by half the IPD).
inline Vec3 EyeMidpoint(Vec3 left_pos, Vec3 right_pos) {
  return VMid(left_pos, right_pos);
}
inline float EyeSeparation(Vec3 left_pos, Vec3 right_pos) {
  Vec3 d{left_pos.x - right_pos.x, left_pos.y - right_pos.y,
         left_pos.z - right_pos.z};
  return VLen(d);
}
// Signed offset of an eye from the head midpoint along the body right
// vector: negative for the left eye, positive for the right (game basis,
// fwd = +Z). H3's per-eye view offset consumes this; a sign error here is
// cross-eyed stereo, so it is pinned by tests.
inline float EyeOffsetAlongRight(Vec3 eye_pos, Vec3 head_mid, Vec3 body_right) {
  Vec3 rel{eye_pos.x - head_mid.x, eye_pos.y - head_mid.y,
           eye_pos.z - head_mid.z};
  return VDot(rel, body_right);
}

// Native shader rows, as consumed by dp4 (not the transposed CPU camera object).
// Only a camera-relative LH 0..1-depth center projection
// is supported. This is a per-view transform, never a pass/camera classifier.
// Eye origin/basis are expressed in CENTER VIEW axes, in native scene units;
// the caller must establish XR-to-native scale and orientation before calling.
// Keep the center-relative model/bone palettes unchanged. Moving those as well
// would apply the eye translation twice.
inline bool NativeEyeClip(const float center[16], const EyeFrustum& eye,
                          Vec3 origin, Vec3 right, Vec3 up, Vec3 forward,
                          float out[16]) {
  float m[16];  // allow center == out without destroying the input
  for (unsigned i = 0; i < 16; ++i) m[i] = center[i];
  for (unsigned i = 0; i < 16; ++i) out[i] = 0;
  for (float v : m) if (!std::isfinite(v)) return false;
  const float scalars[] = {eye.tan_left, eye.tan_right, eye.tan_up,
      eye.tan_down, origin.x, origin.y, origin.z,
      right.x, right.y, right.z, up.x, up.y, up.z,
      forward.x, forward.y, forward.z};
  for (float v : scalars) if (!std::isfinite(v)) return false;
  const float width = eye.tan_right - eye.tan_left;
  const float height = eye.tan_up - eye.tan_down;
  if (!std::isfinite(width) || !std::isfinite(height) ||
      width <= 1e-6f || height <= 1e-6f) return false;
  auto rotation = [](Vec3 r, Vec3 u, Vec3 f) {
    constexpr float e = 0.0001f;
    return std::fabs(VDot(r,r)-1) < e && std::fabs(VDot(u,u)-1) < e &&
        std::fabs(VDot(f,f)-1) < e && std::fabs(VDot(r,u)) < e &&
        std::fabs(VDot(r,f)) < e && std::fabs(VDot(u,f)) < e &&
        VDot(r, VCross(u,f)) > 1-e;
  };
  if (!rotation(right, up, forward)) return false;
  const Vec3 f{m[12],m[13],m[14]};
  const float f2 = VDot(f,f);
  if (std::fabs(f2-1) >= .0001f) return false;
  // Main-scene rows include a small off-axis term. Remove it before
  // extracting rotation; normalizing the full rows would skew eye offsets.
  const float cx = VDot({m[0],m[1],m[2]},f)/f2;
  const float cy = VDot({m[4],m[5],m[6]},f)/f2;
  const Vec3 rx{m[0]-cx*f.x,m[1]-cx*f.y,m[2]-cx*f.z};
  const Vec3 uy{m[4]-cy*f.x,m[5]-cy*f.y,m[6]-cy*f.z};
  const float sx = VLen(rx);
  const float sy = VLen(uy);
  if (!std::isfinite(sx) || !std::isfinite(sy) || sx <= 1e-6f || sy <= 1e-6f)
    return false;
  const Vec3 r{rx.x/sx,rx.y/sx,rx.z/sx};
  const Vec3 u{uy.x/sy,uy.y/sy,uy.z/sy};
  if (!rotation(r,u,f) || m[3] != 0 || m[7] != 0 || m[15] != 0)
    return false;
  const float a = VDot({m[8],m[9],m[10]},f)/f2, b = m[11];
  if (a < 1 || b >= 0) return false;  // finite/infinite forward-Z, not reversed-Z
  for (unsigned i = 0; i < 3; ++i) {
    if (std::fabs(m[8+i] - a*m[12+i]) > 0.0001f) return false;
  }
  auto world = [&](Vec3 v) {
    return Vec3{v.x*r.x+v.y*u.x+v.z*f.x,
                v.x*r.y+v.y*u.y+v.z*f.y,
                v.x*r.z+v.y*u.z+v.z*f.z};
  };
  const Vec3 er = world(right), eu = world(up), ef = world(forward);
  const float x = 2/width, y = 2/height;
  const float ox = -(eye.tan_right+eye.tan_left)/width;
  const float oy = -(eye.tan_up+eye.tan_down)/height;
  const float tx = -VDot(right,origin), ty = -VDot(up,origin);
  const float tz = -VDot(forward,origin);
  const float rows[16] = {
      x*er.x+ox*ef.x, x*er.y+ox*ef.y, x*er.z+ox*ef.z, x*tx+ox*tz,
      y*eu.x+oy*ef.x, y*eu.y+oy*ef.y, y*eu.z+oy*ef.z, y*ty+oy*tz,
      a*ef.x, a*ef.y, a*ef.z, a*tz+b,
      ef.x, ef.y, ef.z, tz};
  for (float v : rows) if (!std::isfinite(v)) return false;
  for (unsigned i = 0; i < 16; ++i) out[i] = rows[i];
  return true;
}

}  // namespace dsvr
