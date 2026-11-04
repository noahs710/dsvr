#pragma once

// Inertial weapon lag: monotone angular response (lane B bench follow-up).
//
// Design adapted from ERVR's StepWeaponOrientation in weapon_motion.h
// (GPL-3.0, (c) Ilya Mezerowsky): weight resists bending the shaft about
// the grip while roll about the shaft stays under control, with no stored
// spring velocity to fling the weapon past the hand. Reimplemented for
// DSVR's AimQuat/Vec3; no ERVR code is copied. Pure per-frame stepper; the
// consumer seam is the avatar support-hand/weapon solve, NOT wired yet —
// landing the math + tests first, feel-tuning with a headset pass.
//
// Conventions match cast_aim_math.h (x, y, z, w Hamilton quats, play space).

#include <cmath>

#include "cast_aim_math.h"

namespace dsvr {

// One lag step toward target. weight <= 0 (or a bad dt) returns target
// unchanged: simulated resistance never invents motion, and the tracking
// constraint wins over any stale state.
inline AimQuat StepWeaponLag(AimQuat current, AimQuat target, Vec3 shaft_axis,
                             float dt, float weight, float reach, float com,
                             float weight_ref, float com_ref, float spring_hz,
                             float angular_accel, float torque, float brake,
                             float max_lag) {
  current = AimQuatNorm(current);
  target = AimQuatNorm(target);
  if (!(dt > 0.0f && dt <= 0.1f) || !(weight > 0.0f)) return target;
  AimQuat lag = AimQuatNorm(AimQuatMul(AimQuatConj(target), current));
  // Decompose in weapon space so the long lever does not resist alignment.
  lag = AimQuatNorm(
      AimQuatMul(lag, AimQuatConj(AimQuatTwist(lag, shaft_axis))));
  const Vec3 bend = AimQuatRotationVector(lag);
  const float angle = VLen(bend);
  if (!(angle > 1e-6f)) return target;
  // Include distributed mass: a centre-gripped weapon still resists bending.
  const float moment =
      com * com > reach * reach / 12.0f ? com * com : reach * reach / 12.0f;
  const float ref = com_ref > 0.05f ? com_ref : 0.05f;
  const float resistance =
      1.0f + (weight > 0.0f ? weight : 0.0f) /
                 (weight_ref > 0.1f ? weight_ref : 0.1f) * moment /
                 (ref * ref);
  const float assistance = torque > 0.1f ? torque : 0.1f;
  const float response =
      6.2831853f * (spring_hz > 0.5f ? spring_hz : 0.5f) *
      std::sqrt(assistance / resistance);
  const float accel =
      (angular_accel > 1.0f ? angular_accel : 1.0f) * assistance / resistance;
  const float exp_step = angle * (1.0f - std::exp(-response * dt));
  const float brake_step =
      std::sqrt(2.0f * (brake > 1.0f ? brake : 1.0f) * accel * angle) * dt;
  const float step = exp_step < brake_step ? exp_step : brake_step;
  // Old configs allowed large separations; the tracking constraint wins over
  // simulated resistance. Clamp the held lag to [0.1, 45] degrees.
  const float clamped_lag =
      max_lag < 0.1f ? 0.1f : (max_lag > 0.7853982f ? 0.7853982f : max_lag);
  const float remaining = angle - step;
  const float held =
      remaining < 0.0f ? 0.0f : (remaining > clamped_lag ? clamped_lag
                                                         : remaining);
  return AimQuatNorm(
      AimQuatMul(target, AimQuatAxisAngle(VScale(bend, held / angle))));
}

}  // namespace dsvr
