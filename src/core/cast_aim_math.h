#pragma once

// Controller-directed spell/bow aim, input side (lane C, slice C2).
//
// Design adapted from ERVR (Elden Ring VR, GPL-3.0, (c) Ilya Mezerowsky):
// TwoHandAimState (grip-line steering) and the cast-aim-hand selection
// (press window + passive equipment hand). Reimplemented here for DSVR's
// Vec3/float-quat types; no ERVR code is copied. The native half of ERVR's
// aim (relaxing the game's own SetBulletAimAngle limits) needs DSR's
// traced aim seam and is NOT faked here: this header decides WHICH hand
// aims and HOW a two-handed bow steers, and publishes it for the avatar
// and the future seam to consume.
//
// Convention: quats are (x, y, z, w), Hamilton product, rotate(v) = q v q*.
// All vectors are in one play space shared by both hands.

#include <cmath>

#include "camera_math.h"

namespace dsvr {

struct AimQuat {
  float x = 0, y = 0, z = 0, w = 1;
};

inline AimQuat AimQuatNorm(AimQuat q) {
  const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  if (!(n > 1e-9f) || !std::isfinite(n)) return {0, 0, 0, 1};
  return {q.x / n, q.y / n, q.z / n, q.w / n};
}

inline AimQuat AimQuatMul(AimQuat a, AimQuat b) {
  return {
      a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
      a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
      a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
      a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

inline AimQuat AimQuatConj(AimQuat q) { return {-q.x, -q.y, -q.z, q.w}; }

inline Vec3 AimQuatRotate(AimQuat q, Vec3 v) {
  // q v q* expanded; q is normalized by the caller path (Update normalizes).
  const Vec3 qv{q.x, q.y, q.z};
  const Vec3 uv{VCross(qv, v)};
  const Vec3 uuv{VCross(qv, uv)};
  return {v.x + 2.f * (q.w * uv.x + uuv.x),
          v.y + 2.f * (q.w * uv.y + uuv.y),
          v.z + 2.f * (q.w * uv.z + uuv.z)};
}

// Shortest-arc rotation taking unit a to unit b. Near-antipodal pairs have
// no stable axis; the caller holds through those (as ERVR does) rather than
// calling here.
inline AimQuat AimQuatFromTo(Vec3 a, Vec3 b) {
  const float d = VDot(a, b);
  if (d > 1.f - 1e-6f) return {0, 0, 0, 1};
  Vec3 axis = VCross(a, b);
  const float n = VLen(axis);
  if (!(n > 1e-6f)) return {0, 0, 0, 1};
  const float half = std::atan2(n, d) * 0.5f;
  const float s = std::sin(half) / n;
  return AimQuatNorm({axis.x * s, axis.y * s, axis.z * s, std::cos(half)});
}

inline AimQuat AimQuatAxisAngle(Vec3 axis_angle) {
  const float angle = VLen(axis_angle);
  if (!(angle > 1e-9f)) return {0, 0, 0, 1};
  const float half = angle * 0.5f;
  const float s = std::sin(half) / angle;
  return {axis_angle.x * s, axis_angle.y * s, axis_angle.z * s,
          std::cos(half)};
}

// Rotation vector (axis * angle) of q, the log map. Used for the steering
// clamp and the exponential approach step.
inline Vec3 AimQuatRotationVector(AimQuat q) {
  q = AimQuatNorm(q);
  if (q.w < 0) q = {-q.x, -q.y, -q.z, -q.w};
  const Vec3 v{q.x, q.y, q.z};
  const float n = VLen(v);
  if (!(n > 1e-6f)) return {0, 0, 0};
  return VScale(v, 2.f * std::atan2(n, q.w) / n);
}

// Twist of q about unit axis: the part of the rotation that spins around the
// shaft. Hand separation defines bending only; removing the twist keeps the
// support hand from rolling the weapon (fought the main wrist in ERVR).
inline AimQuat AimQuatTwist(AimQuat q, Vec3 axis) {
  q = AimQuatNorm(q);
  axis = VNorm(axis);
  const float along = VDot({q.x, q.y, q.z}, axis);
  return AimQuatNorm({axis.x * along, axis.y * along, axis.z * along, q.w});
}

// Two-handed bow steering. The grip line (support minus weapon hand) defines
// the aim: when the hands separate after a grab, the weapon steers toward
// the line; ordinary wrist motion stays 1:1 and only the bend is corrected.
struct BowAimSteer {
  bool held = false;
  Vec3 grip_line{};
  AimQuat correction{0, 0, 0, 1};

  void Reset() {
    held = false;
    grip_line = {};
    correction = {0, 0, 0, 1};
  }

  // wrist: weapon-hand orientation. line: support minus weapon position.
  // shaft_axis: weapon long axis in wrist space (bow: string-normal plane
  // is handled by the caller; default is the controller handle axis).
  // Returns the steered weapon-hand orientation.
  AimQuat Update(AimQuat wrist, Vec3 line, bool grabbed, float dt,
                 Vec3 shaft_axis = {0, 1, 0}) {
    wrist = AimQuatNorm(wrist);
    if (!(dt > 0.f && dt <= 0.1f)) {
      held = false;
      correction = {0, 0, 0, 1};
    }
    const float length = VLen(line);
    if (!grabbed) held = false;
    // Coincident controllers do not define an aiming direction. Keep the
    // last correction through a short overlap; acquire on separation.
    if (grabbed && !held && length >= 0.06f) {
      grip_line = AimQuatRotate(AimQuatConj(AimQuatMul(wrist, correction)),
                                VScale(line, 1.f / length));
      held = true;
    }
    AimQuat target = grabbed ? correction : AimQuat{0, 0, 0, 1};
    if (grabbed && held && length >= 0.06f) {
      const Vec3 direction = AimQuatRotate(AimQuatConj(wrist),
                                           VScale(line, 1.f / length));
      // Near an antipodal line the shortest rotation has no stable axis.
      // Hold through it instead of choosing an arbitrary 180-degree flip.
      if (VDot(grip_line, direction) > -0.95f) {
        AimQuat steer = AimQuatFromTo(grip_line, direction);
        steer = AimQuatNorm(AimQuatMul(
            steer, AimQuatConj(AimQuatTwist(steer, shaft_axis))));
        const Vec3 rv = AimQuatRotationVector(steer);
        const float angle = VLen(rv);
        if (angle > 1e-6f) {
          const float clamped = angle > 1.3962634f ? 1.3962634f : angle;  // 80 deg
          target = AimQuatAxisAngle(VScale(rv, clamped / angle));
        } else {
          target = {0, 0, 0, 1};
        }
      }
    }
    // Smooth steering and release even with a light weapon. Limit the
    // correction speed; ordinary wrist motion stays 1:1.
    if (dt > 0.f && dt <= 0.1f) {
      const Vec3 delta =
          AimQuatRotationVector(AimQuatNorm(AimQuatMul(target, AimQuatConj(correction))));
      const float angle = VLen(delta);
      if (angle > 1e-6f) {
        const float step = angle * (1.f - std::exp(-dt / 0.045f));
        const float max_step = 6.2831853f * dt;
        const float applied = step > max_step ? max_step : step;
        correction = AimQuatNorm(AimQuatMul(
            AimQuatAxisAngle(VScale(delta, applied / angle)), correction));
      } else {
        correction = target;
      }
    }
    correction = AimQuatNorm(AimQuatMul(
        correction, AimQuatConj(AimQuatTwist(correction, shaft_axis))));
    return AimQuatNorm(AimQuatMul(wrist, correction));
  }

  bool Active() const {
    return held || VLen(AimQuatRotationVector(correction)) > 1e-5f;
  }
};

// Which hand aims ranged effects right now. A press (attack/skill button the
// game was actually handed) makes that hand the aim hand for the cast window;
// while no press is live, the hand passively holding a ranged weapon aims;
// otherwise -1 (the head aims, as before). Thread-safety is the caller's
// (input thread owns it in production); all state is plain data for tests.
struct CastAimSelector {
  int press_hand = -1;
  double press_at = -1e9;
  double live_until = 0.0;
  int passive_hand = -1;  // hand holding a catalyst/bow, -1 = none

  void Reset() {
    press_hand = -1;
    press_at = -1e9;
    live_until = 0.0;
    passive_hand = -1;
  }

  void NotePress(int hand, double now_s, double window_s) {
    if (hand < 0 || hand > 1 || !(now_s >= 0.0) || !(window_s > 0.0)) return;
    press_hand = hand;
    press_at = now_s;
    live_until = now_s + window_s;
  }

  void SetPassiveHand(int hand) {
    passive_hand = (hand == 0 || hand == 1) ? hand : -1;
  }

  // -1 = none (head aims), else 0 = left, 1 = right. casting is true while a
  // press owns the window (the game-facing cast-aim-live signal). The stored
  // expiry governs: the caller arms it with the configured window.
  int AimHand(double now_s, bool* casting = nullptr) const {
    const bool live = press_hand >= 0 && now_s >= press_at &&
                      now_s <= live_until && std::isfinite(live_until);
    if (casting) *casting = live;
    if (live) return press_hand;
    return passive_hand;
  }
};

}  // namespace dsvr
