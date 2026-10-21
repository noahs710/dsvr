#pragma once
#include <cmath>

#include "camera_math.h"

namespace dsvr {

// Pure, unit-tested two-bone IK for driving the *actual player model's*
// arms from OpenXR hand poses (approved design 2026-10-13). Runtime writes in
// arm_ik.cpp require the verified PlayerIns vtable seam, a live FLVER rig, and
// the post-writer output/palette alias check. Everything testable without a
// headset -- reach clamping, elbow placement, XR mapping, and smoothing -- is
// decided here; live body quality remains separately unverified.
//
// Conventions:
// - World/player space uses the game basis (right/up/forward rows,
//   forward = character facing, +Y world up), metres.
// - XR LOCAL space: +X right, +Y up, -Z forward (OpenXR). The hand
//   target is head-relative so arm reach is measured from the head,
//   exactly as a real arm is measured from the shoulder near it.
// - Quaternions are xyzw. Bone aiming is expressed as a from->to
//   rotation so arm_ik.cpp can compose it with each bone's rest pose
//   once the skeleton convention is verified -- no bone-axis guess is
//   baked in here.

struct ArmRig {
  float shoulder_height_m = 1.42f;  // above coherent character origin
  float shoulder_width_m = 0.21f;   // lateral offset from body centre
  float shoulder_forward_m = 0.02f;
  float upper_length_m = 0.28f;     // shoulder -> elbow
  float forearm_length_m = 0.30f;   // elbow -> wrist
};

struct IkSolution {
  Vec3 shoulder{0, 0, 0};
  Vec3 elbow{0, 0, 0};
  Vec3 wrist{0, 0, 0};   // reach-clamped target actually achieved
  Vec3 upper_dir{0, 0, 1};
  Vec3 forearm_dir{0, 0, 1};
  bool reached = false;  // raw target lies inside the reachable annulus
  bool valid = false;
};

inline bool FiniteVec(Vec3 v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// Shoulder anchor on the body: character origin + basis offsets. `side`
// is +1 for the body's right, -1 for its left, in the body-right axis.
inline Vec3 ShoulderPosition(Vec3 player_pos, Vec3 body_right, Vec3 body_up,
                             Vec3 body_fwd, float side,
                             const ArmRig& rig = {}) {
  return VAdd(VAdd(VAdd(player_pos, VScale(body_up, rig.shoulder_height_m)),
                   VScale(body_right, side * rig.shoulder_width_m)),
              VScale(body_fwd, rig.shoulder_forward_m));
}

// Elbow pole: down, slightly back, and outward, so elbows point where
// human elbows point when holding controllers (never into the torso).
inline Vec3 ElbowPoleDirection(Vec3 body_right, Vec3 body_up, Vec3 body_fwd,
                               float side) {
  Vec3 p = VAdd(VAdd(VScale(body_up, -1.0f), VScale(body_fwd, -0.35f)),
                VScale(body_right, side * 0.45f));
  return VLen(p) > 1e-6f ? VNorm(p) : Vec3{0, -1, 0};
}

// XR hand target in world space: head world anchor plus the hand's
// offset from the head, expressed in the body basis (reuses the
// camera's HeadTranslationWorld mapping: XR -Z maps to body forward).
inline Vec3 HandTargetWorld(Vec3 hand_xr, Vec3 head_xr, Vec3 head_world,
                            Vec3 body_right, Vec3 body_up, Vec3 body_fwd,
                            float world_scale) {
  if (!FiniteVec(hand_xr) || !FiniteVec(head_xr) || !FiniteVec(head_world))
    return head_world;
  Vec3 delta{hand_xr.x - head_xr.x, hand_xr.y - head_xr.y,
             hand_xr.z - head_xr.z};
  return VAdd(head_world, HeadTranslationWorld(delta, body_right, body_up,
                                               body_fwd, world_scale));
}

// Classic two-bone IK by the law of cosines. The wrist is clamped onto
// the reachable sphere; `pole_dir` picks the elbow bend plane. Fails
// closed (valid=false) on non-finite input or degenerate lengths.
inline IkSolution SolveTwoBoneIk(Vec3 shoulder, Vec3 target, Vec3 pole_dir,
                                 const ArmRig& rig = {}) {
  IkSolution out;
  out.shoulder = shoulder;
  if (!FiniteVec(shoulder) || !FiniteVec(target) || !FiniteVec(pole_dir) ||
      !std::isfinite(rig.upper_length_m) ||
      !std::isfinite(rig.forearm_length_m) ||
      !(rig.upper_length_m > 1e-4f) || !(rig.forearm_length_m > 1e-4f))
    return out;
  const float l1 = rig.upper_length_m, l2 = rig.forearm_length_m;
  Vec3 to{target.x - shoulder.x, target.y - shoulder.y,
          target.z - shoulder.z};
  float dist = VLen(to);
  const float max_reach = l1 + l2;
  const float min_reach = std::fabs(l1 - l2);
  if (!std::isfinite(dist) || !std::isfinite(max_reach)) return out;
  out.reached = dist >= min_reach && dist <= max_reach;
  float clamped = dist;
  if (clamped > max_reach) clamped = max_reach;
  // Equal lengths can fold onto the shoulder. Keep the law-of-cosines
  // denominator nonzero without stretching either link.
  const float solve_min = min_reach > 1e-6f ? min_reach : 1e-6f;
  if (clamped < solve_min) clamped = solve_min;
  Vec3 dir = dist > 1e-6f ? VScale(to, 1.0f / dist) : Vec3{0, 0, 1};
  Vec3 wrist = VAdd(shoulder, VScale(dir, clamped));
  float a = (l1 * l1 - l2 * l2 + clamped * clamped) / (2.0f * clamped);
  float h2 = l1 * l1 - a * a;
  float h = h2 > 0.0f ? std::sqrt(h2) : 0.0f;
  Vec3 bend = VAdd(pole_dir, VScale(dir, -VDot(pole_dir, dir)));
  if (VLen(bend) < 1e-5f) {  // pole parallel to arm: pick any perpendicular
    Vec3 arb = std::fabs(dir.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    bend = VAdd(arb, VScale(dir, -VDot(arb, dir)));
  }
  bend = VNorm(bend);
  out.elbow = VAdd(VAdd(shoulder, VScale(dir, a)), VScale(bend, h));
  out.wrist = wrist;
  out.upper_dir = VNorm(Vec3{out.elbow.x - shoulder.x,
                             out.elbow.y - shoulder.y,
                             out.elbow.z - shoulder.z});
  out.forearm_dir = VNorm(Vec3{wrist.x - out.elbow.x, wrist.y - out.elbow.y,
                               wrist.z - out.elbow.z});
  out.valid = FiniteVec(out.elbow) && FiniteVec(out.wrist) &&
              FiniteVec(out.upper_dir) && FiniteVec(out.forearm_dir);
  return out;
}

// Shortest-arc quaternion (xyzw) rotating unit `from` onto unit `to`.
inline void QuatFromTo(Vec3 from, Vec3 to, float out_xyzw[4]) {
  // Invalid/rest-axis input must never yield a non-unit quaternion that
  // later corrupts a verified bone transform.
  out_xyzw[0] = out_xyzw[1] = out_xyzw[2] = 0.0f;
  out_xyzw[3] = 1.0f;
  if (!FiniteVec(from) || !FiniteVec(to) ||
      !(VLen(from) > 1e-6f) || !(VLen(to) > 1e-6f) ||
      !std::isfinite(VLen(from)) || !std::isfinite(VLen(to))) return;
  Vec3 f = VNorm(from), t = VNorm(to);
  float d = VDot(f, t);
  if (d > 0.99999f) {
    out_xyzw[0] = out_xyzw[1] = out_xyzw[2] = 0.0f;
    out_xyzw[3] = 1.0f;
    return;
  }
  if (d < -0.99999f) {  // opposite: rotate 180 deg about any perpendicular
    Vec3 arb = std::fabs(f.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    Vec3 axis = VNorm(VCross(f, arb));
    out_xyzw[0] = axis.x; out_xyzw[1] = axis.y; out_xyzw[2] = axis.z;
    out_xyzw[3] = 0.0f;
    return;
  }
  Vec3 axis = VCross(f, t);
  float s = std::sqrt((1.0f + d) * 2.0f);
  out_xyzw[0] = axis.x / s; out_xyzw[1] = axis.y / s;
  out_xyzw[2] = axis.z / s; out_xyzw[3] = s * 0.5f;
}

// Frame-rate independent exponential smoothing (lambda ~18 = responsive
// without hiding the hand's own motion; no smoothing across teleports
// is the caller's job via the velocity teleport guard).
inline Vec3 ExpSmooth(Vec3 current, Vec3 target, float dt, float lambda) {
  if (!std::isfinite(dt) || !std::isfinite(lambda) || !(dt > 0.0f) ||
      !(lambda >= 0.0f) || !FiniteVec(current) || !FiniteVec(target))
    return target;
  float k = 1.0f - std::exp(-lambda * dt);
  return VAdd(current, VScale(Vec3{target.x - current.x,
                                   target.y - current.y,
                                   target.z - current.z}, k));
}

}  // namespace dsvr
