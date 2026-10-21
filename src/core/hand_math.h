#pragma once
#include <cmath>

#include "camera_math.h"

namespace dsvr {

// Pure, unit-tested hand/blade geometry for motion input (P4).
// Runtime pose fetching (OpenXR action spaces) stays in input.cpp; this
// module owns every decision that can be wrong without a headset to notice:
// which way the blade points, where its tip is, and when the off-hand
// has a block / parry / two-hand candidate pose (docs/ENGINEERING.md).
// These predicates do not implement native combat outcomes.
//
// Conventions (OpenXR LOCAL space, metres, right-handed, -Z forward):
// - A grip pose is (position, orientation quat xyzw). The blade leaves the
//   grip along grip-local -Z: the direction the fingers point when holding
//   a controller. This matches the XR aim/grip forward convention used by
//   Quest Touch and Index; the first headset test (H4) verifies it by
//   logging BladeDirection for a known physical pose and may flip exactly
//   this one constant -- nothing else derives blade direction.
// - Speeds are smoothed hand speeds from GestureTracker (m/s). Raw pose
//   finite differences must never feed these predicates directly.

inline Vec3 BladeDirection(float qx, float qy, float qz, float qw) {
  Vec3 d = QuatRotate({0, 0, -1}, qx, qy, qz, qw);
  float l = VLen(d);
  return l > 1e-6f ? VScale(d, 1.0f / l) : Vec3{0, 0, -1};
}

inline Vec3 BladeTip(Vec3 grip_pos, float qx, float qy, float qz, float qw,
                     float blade_length_m) {
  Vec3 d = BladeDirection(qx, qy, qz, qw);
  return VAdd(grip_pos, VScale(d, blade_length_m));
}

// Weapon-hand index: 0 = left, 1 = right. Config dominant_hand_left picks
// the weapon; the other hand is the off-hand (shield / parry / second grip).
inline int WeaponHandIndex(bool dominant_left) { return dominant_left ? 0 : 1; }
inline int OffHandIndex(bool dominant_left) { return dominant_left ? 1 : 0; }

struct HandParams {
  float blade_length_m = 0.9f;      // longsword-class reach, grip -> tip
  float two_hand_distance_m = 0.14f;  // grips closer than this = two-handed
  float block_max_speed = 0.8f;     // held guard, not a sweep (m/s)
  float block_min_forward_m = 0.05f;  // off-hand ahead of the head plane
  float block_min_height_m = -0.45f;  // off-hand no lower than this vs head
  float parry_speed_min = 2.2f;     // fast off-hand sweep (m/s)
};

// Off-hand guard: hand held (slow) in front of and near head height.
// head_fwd is the unit body forward (captured basis forward, not gaze
// pitch), so looking down never drops the guard. Returns false on any
// non-finite input instead of guessing.
inline bool IsBlocking(Vec3 off_pos, float off_speed, Vec3 head_pos,
                       Vec3 head_fwd, const HandParams& p = {}) {
  if (!std::isfinite(off_pos.x) || !std::isfinite(off_pos.y) ||
      !std::isfinite(off_pos.z) || !std::isfinite(off_speed) ||
      !std::isfinite(head_pos.x) || !std::isfinite(head_pos.y) ||
      !std::isfinite(head_pos.z) || !std::isfinite(head_fwd.x) ||
        !std::isfinite(head_fwd.y) || !std::isfinite(head_fwd.z) ||
        !std::isfinite(p.block_max_speed) || !std::isfinite(p.block_min_forward_m) ||
      !std::isfinite(p.block_min_height_m))
    return false;
  const float forward_length = VLen(head_fwd);
  if (!(forward_length > 1e-6f) || off_speed < 0.0f ||
      off_speed > p.block_max_speed) return false;
  head_fwd = VScale(head_fwd, 1.0f / forward_length);
  Vec3 rel{off_pos.x - head_pos.x, off_pos.y - head_pos.y,
           off_pos.z - head_pos.z};
  float fwd = VDot(rel, head_fwd);
  if (fwd < p.block_min_forward_m) return false;
  if (rel.y < p.block_min_height_m) return false;
  return true;
}

// Parry candidate: a fast off-hand sweep. This is only a CANDIDATE -- the
// combat layer (P5/H5) must still confirm an incoming attack window before
// any parry is credited. Never credit a parry from this predicate alone.
inline bool IsParryCandidate(float off_speed, const HandParams& p = {}) {
  return std::isfinite(off_speed) && off_speed >= p.parry_speed_min;
}

// Two-handed stance: off-hand grip held near the weapon grip while its
// squeeze is held. Distance alone never two-hands (hands cross constantly
// in normal play); the squeeze requirement is deliberate.
inline bool IsTwoHanded(Vec3 weapon_pos, Vec3 off_pos, bool off_squeeze_held,
                        const HandParams& p = {}) {
  if (!off_squeeze_held) return false;
  if (!std::isfinite(weapon_pos.x) || !std::isfinite(off_pos.x))
    return false;
  Vec3 d{weapon_pos.x - off_pos.x, weapon_pos.y - off_pos.y,
         weapon_pos.z - off_pos.z};
  return VLen(d) <= p.two_hand_distance_m;
}

}  // namespace dsvr
