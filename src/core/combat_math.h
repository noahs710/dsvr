#pragma once
#include <cmath>
#include <cstdint>
#include <span>

#include "arm_pose_math.h"
#include "camera_math.h"
#include "gesture_math.h"

namespace dsvr {

// Pure, unit-tested combat numbers for the P5 slice. The runtime (H5) owns
// hit-volume ownership and the game's damage dispatch; this module owns
// the three mappings that must never be re-derived at the hit site:
//   speed -> damage scale, blade capsule -> overlap, hit -> haptic pulse.
// Damage fires only on measured overlap at speed (docs/ENGINEERING.md);
// nothing here fires on a timer or an animation window.

struct CombatParams {
  float full_speed = 4.0f;      // m/s at which the scale saturates
  float min_multiplier = 0.6f;  // scale exactly at the firing threshold
  float max_multiplier = 1.8f;  // scale at/above full_speed
  float thrust_bonus = 1.15f;   // piercing premium over an equal-speed slash
  float blade_radius_m = 0.03f; // blade capsule radius for overlap tests
};

// Velocity -> damage multiplier. 0 below the kind's firing threshold (the
// caller passes GestureTracker::PeakSpeed and the threshold that fired),
// then linear min->max between threshold and full_speed, clamped. The
// config gain ([combat] swing_damage_gain) multiplies last so player
// tuning can never resurrect a sub-threshold wave (0 * gain stays 0).
// Non-finite input scales to 0 (fail closed: no phantom damage).
inline float DamageScale(float peak_speed, GestureKind kind,
                         float firing_threshold, float gain,
                         const CombatParams& p = {}) {
  if (!std::isfinite(peak_speed) || !std::isfinite(gain) ||
      !(firing_threshold > 0.0f) || peak_speed < firing_threshold)
    return 0.0f;
  float span = p.full_speed - firing_threshold;
  float t = span > 1e-4f ? (peak_speed - firing_threshold) / span : 1.0f;
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  float scale = p.min_multiplier + (p.max_multiplier - p.min_multiplier) * t;
  if (kind == GestureKind::Thrust) scale *= p.thrust_bonus;
  scale *= gain;
  return std::isfinite(scale) ? scale : 0.0f;
}

// Frame displacement converted to metres per second, independent of render rate.
// Invalid/gap samples fail closed instead of creating a combat-speed spike.
inline float DisplacementSpeed(Vec3 previous, Vec3 current, float dt) {
  if (!std::isfinite(previous.x) || !std::isfinite(previous.y) ||
      !std::isfinite(previous.z) || !std::isfinite(current.x) ||
      !std::isfinite(current.y) || !std::isfinite(current.z) ||
      !(dt > 0.0f) || dt > 0.25f || !std::isfinite(dt))
    return 0.0f;
  const Vec3 delta{current.x - previous.x, current.y - previous.y,
                   current.z - previous.z};
  const float speed = VLen(delta) / dt;
  return std::isfinite(speed) && speed <= 25.0f ? speed : 0.0f;
}

// Shortest distance from point p to segment [a,b]. Degenerate segments
// (a == b, a zero-length blade) collapse to a point distance.
inline float SegmentPointDistance(Vec3 a, Vec3 b, Vec3 p) {
  Vec3 ab{b.x - a.x, b.y - a.y, b.z - a.z};
  Vec3 ap{p.x - a.x, p.y - a.y, p.z - a.z};
  float len2 = VDot(ab, ab);
  float t = len2 > 1e-12f ? VDot(ap, ab) / len2 : 0.0f;
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  Vec3 closest{a.x + ab.x * t, a.y + ab.y * t, a.z + ab.z * t};
  Vec3 d{p.x - closest.x, p.y - closest.y, p.z - closest.z};
  return VLen(d);
}

// Blade capsule (grip -> tip, blade_radius) vs sphere (centre, radius).
// Inclusive boundary counts as overlap: a grazing touch at speed is a hit
// the player felt connect; the damage scale already handles weakness.
inline bool BladeOverlapsSphere(Vec3 grip, Vec3 tip, Vec3 centre,
                                float sphere_radius,
                                const CombatParams& p = {}) {
  if (!FiniteVec(grip) || !FiniteVec(tip) || !FiniteVec(centre) ||
      !(sphere_radius >= 0.0f) || !std::isfinite(p.blade_radius_m) ||
      p.blade_radius_m < 0.0f)
    return false;
  return SegmentPointDistance(grip, tip, centre) <=
         sphere_radius + p.blade_radius_m;
}

// --- Fluid combat (IK design, 2026-10-13): no swing classifier ----
// The blade is the hand. Every frame the runtime feeds grip+tip; this
// tracker owns only smoothed velocity and the swept tip segment (prev
// -> current tips), so a fast swing can never tunnel past an enemy
// between frames. Nothing fires, latches, or re-arms here: damage is
// DamageScaleContinuous(tip_speed) gated by measured overlap and a
// per-target re-hit window, evaluated continuously.

struct BladeKinematics {
  Vec3 grip{0, 0, 0};
  Vec3 tip{0, 0, 0};
  Vec3 prev_tip{0, 0, 0};
  Vec3 prev_grip{0, 0, 0};
  Vec3 grip_velocity{0, 0, 0};
  Vec3 tip_velocity{0, 0, 0};
  float tip_speed = 0.0f;
  float grip_speed = 0.0f;
  bool valid = false;
  Vec3 head{0, 0, 0};  // last head position (FeedRelative only)
  std::uint64_t space_epoch_ = 0;
  bool head_primed_ = false;

  void Reset() { *this = BladeKinematics{}; }

  // dt <= 0 / non-finite / >25 m/s teleports reset without a phantom
  // speed spike (same guard as the retired gesture tracker).
  void Feed(Vec3 new_grip, Vec3 new_tip, float dt) {
    // Switching feed modes starts a new velocity history.
    if (head_primed_) Reset();
    FeedSample(new_grip, new_tip, dt);
  }

 private:
  // True only when a continuous sample contributed velocity. Priming,
  // invalid input and teleports must not receive a head correction.
  bool FeedSample(Vec3 new_grip, Vec3 new_tip, float dt) {
    if (!(dt > 0.0f) || dt > 0.25f || !FiniteVec(new_grip) ||
        !FiniteVec(new_tip)) {
      valid = false;
      grip_velocity = tip_velocity = {0, 0, 0};
      tip_speed = grip_speed = 0.0f;
      return false;
    }
    const bool advanced = valid;
    if (valid) {
      const Vec3 raw_g = VScale(VAdd(new_grip, VScale(grip, -1)), 1 / dt);
      const Vec3 raw_t = VScale(VAdd(new_tip, VScale(tip, -1)), 1 / dt);
      if (!FiniteVec(raw_g) || !FiniteVec(raw_t) ||
          VLen(raw_t) > 25.0f || VLen(raw_g) > 25.0f) {
        grip = new_grip; tip = new_tip; prev_tip = new_tip;
        grip_velocity = tip_velocity = {0, 0, 0};
        tip_speed = grip_speed = 0.0f;
        return false;
      }
      grip_velocity = VAdd(VScale(grip_velocity, 0.5f), VScale(raw_g, 0.5f));
      tip_velocity = VAdd(VScale(tip_velocity, 0.5f), VScale(raw_t, 0.5f));
    }
    prev_tip = valid ? tip : new_tip;
    prev_grip = valid ? grip : new_grip;
    grip = new_grip;
    tip = new_tip;
    valid = true;
    tip_speed = VLen(tip_velocity);
    grip_speed = VLen(grip_velocity);
    return advanced;
  }

 public:
  // Roomscale feed: identical to Feed, except velocities are measured
  // relative to the head. Physical walking translates head and hands
  // together in the tracking space; that shared translation is
  // locomotion, not a swing, and must never become swing energy
  // (docs/ENGINEERING.md D). Positions stay absolute so overlap and the
  // tip sweep still track the real blade. Only translation is removed:
  // turning in place still swings the tip around the head, as it should.
  // A space-epoch change (recenter / new reference space), a non-finite
  // head, or a head teleport invalidates the tracker instead of letting
  // the origin shift spike the speed; the next frame only primes.
  void FeedRelative(Vec3 new_grip, Vec3 new_tip, Vec3 new_head, float dt,
                    std::uint64_t space_epoch) {
    if (space_epoch != space_epoch_ || !FiniteVec(new_head)) {
      Reset();
      space_epoch_ = space_epoch;
      if (!FiniteVec(new_head)) return;
      head = new_head;
      FeedSample(new_grip, new_tip, dt);
      head_primed_ = valid;
      return;
    }
    if (!head_primed_ || !(dt > 0.0f) || dt > 0.25f) {
      Reset();
      space_epoch_ = space_epoch;
      FeedSample(new_grip, new_tip, dt);
      head = new_head;
      head_primed_ = valid;
      return;
    }
    const Vec3 head_velocity_raw{
        (new_head.x - head.x) / dt, (new_head.y - head.y) / dt,
        (new_head.z - head.z) / dt};
    if (VLen(head_velocity_raw) > 25.0f) {
      Reset();
      space_epoch_ = space_epoch;
      return;
    }
    const bool advanced = FeedSample(new_grip, new_tip, dt);
    head = new_head;
    head_primed_ = valid;
    if (!advanced) return;
    // Feed folded velocities as 0.5*old + 0.5*raw_absolute. The stored
    // old velocity is already head-relative, so subtracting half the
    // head velocity yields exactly 0.5*old + 0.5*(raw - head).
    grip_velocity = VAdd(grip_velocity, VScale(head_velocity_raw, -0.5f));
    tip_velocity = VAdd(tip_velocity, VScale(head_velocity_raw, -0.5f));
    tip_speed = VLen(tip_velocity);
    grip_speed = VLen(grip_velocity);
  }

  // Per-frame thrust/slash label from velocity-vs-blade alignment.
  // A label only (damage premium), never a trigger.
  GestureKind FrameKind(Vec3 blade_dir) const {
    if (!valid || tip_speed < 1e-4f) return GestureKind::None;
    float bl = VLen(blade_dir);
    if (bl < 1e-4f) return GestureKind::Slash;
    float axis = std::fabs(VDot(VScale(tip_velocity, 1.0f / tip_speed),
                                VScale(blade_dir, 1.0f / bl)));
    return axis >= 0.7f ? GestureKind::Thrust : GestureKind::Slash;
  }
};

// Continuous damage scale: 0 below contact_speed_min (resting contact
// does nothing), then the same min->max ramp as DamageScale. No latch.
inline float DamageScaleContinuous(float tip_speed, GestureKind frame_kind,
                                   float contact_speed_min, float gain,
                                   const CombatParams& p = {}) {
  if (!std::isfinite(tip_speed) || !std::isfinite(gain) ||
      !(contact_speed_min > 0.0f) || tip_speed < contact_speed_min)
    return 0.0f;
  return DamageScale(tip_speed, frame_kind, contact_speed_min, gain, p);
}

// Swept blade vs sphere: capsule grip->tip OR the tip's frame sweep
// prev_tip->tip, whichever comes closer (anti-tunnelling).
inline bool BladeSweptOverlapsSphere(Vec3 grip, Vec3 tip, Vec3 prev_tip,
                                     Vec3 centre, float sphere_radius,
                                     const CombatParams& p = {}) {
  if (BladeOverlapsSphere(grip, tip, centre, sphere_radius, p)) return true;
  if (!FiniteVec(prev_tip) || !FiniteVec(tip) || !FiniteVec(centre) ||
      !(sphere_radius >= 0.0f) || !std::isfinite(p.blade_radius_m) ||
      p.blade_radius_m < 0.0f)
    return false;
  return SegmentPointDistance(prev_tip, tip, centre) <=
         sphere_radius + p.blade_radius_m;
}

// Rotational sweep (C3 continuous geometry): the tip travels an ARC about the
// grip, so the straight prev_tip->tip chord under-covers a fast wrist
// rotation. A small target can sit inside the volume the blade actually
// occupied and still be outside the previous capsule, the current capsule and
// the chord. Subdivide the blade's rotation between the two frames: rotate the
// previous blade direction about the axis joining the two directions, keep
// each sample's own length, and test the capsule plus the tip-to-tip segment at
// every step. Work is bounded (kSweepSteps) so a fast swing cannot expand the
// render callback, and a near-parallel or near-opposite direction pair gets no
// invented rotation plane -- those fall back to the chord test alone.
inline constexpr int kSweepSteps = 4;

inline bool BladeSweptOverlapsSphereArc(Vec3 prev_grip, Vec3 prev_tip,
                                        Vec3 grip, Vec3 tip, Vec3 centre,
                                        float sphere_radius,
                                        const CombatParams& p = {}) {
  if (BladeSweptOverlapsSphere(grip, tip, prev_tip, centre, sphere_radius, p))
    return true;
  if (!FiniteVec(prev_grip) || !FiniteVec(prev_tip) || !FiniteVec(grip) ||
      !FiniteVec(tip) || !FiniteVec(centre) || !(sphere_radius >= 0.0f) ||
      !std::isfinite(p.blade_radius_m) || p.blade_radius_m < 0.0f)
    return false;
  const Vec3 d0{prev_tip.x - prev_grip.x, prev_tip.y - prev_grip.y,
                prev_tip.z - prev_grip.z};
  const Vec3 d1{tip.x - grip.x, tip.y - grip.y, tip.z - grip.z};
  const float l0 = VLen(d0), l1 = VLen(d1);
  if (l0 < 1e-4f || l1 < 1e-4f) return false;
  const Vec3 u0{d0.x / l0, d0.y / l0, d0.z / l0};
  const Vec3 u1{d1.x / l1, d1.y / l1, d1.z / l1};
  float dot = VDot(u0, u1);
  if (dot > 1.0f) dot = 1.0f;
  if (dot < -1.0f) dot = -1.0f;
  if (dot > 0.9999f || dot < -0.9999f) return false;
  const Vec3 axis = VNorm(VCross(u0, u1));
  if (VLen(axis) < 0.5f) return false;
  const float angle = std::acos(dot);
  const float reach = sphere_radius + p.blade_radius_m;
  Vec3 sample_tip = prev_tip;
  for (int step = 1; step <= kSweepSteps; ++step) {
    const float t = float(step) / float(kSweepSteps + 1);
    const Vec3 sample_grip{prev_grip.x + (grip.x - prev_grip.x) * t,
                           prev_grip.y + (grip.y - prev_grip.y) * t,
                           prev_grip.z + (grip.z - prev_grip.z) * t};
    const Vec3 dir = VRotate(u0, axis, angle * t);
    const float length = l0 + (l1 - l0) * t;
    const Vec3 next_tip{sample_grip.x + dir.x * length,
                        sample_grip.y + dir.y * length,
                        sample_grip.z + dir.z * length};
    if (!FiniteVec(next_tip)) return false;
    if (BladeOverlapsSphere(sample_grip, next_tip, centre, sphere_radius, p))
      return true;
    if (SegmentPointDistance(sample_tip, next_tip, centre) <= reach)
      return true;
    sample_tip = next_tip;
  }
  return false;
}

// Per-target re-hit gate: one enemy can be damaged at most once per
// window no matter how long the blade stays inside it, while a swing
// through two enemies hits both. This replaces the old per-swing
// re-arm latch (which made follow-through hits impossible).
class RehitGate {
 public:
  bool TryAcquire(int target_id, long long now_ms, int window_ms) {
    if (target_id < 0 || window_ms <= 0) return false;
    for (auto& e : entries_) {
      if (e.id == target_id) {
        if (now_ms - e.last_ms < window_ms) return false;
        e.last_ms = now_ms;
        return true;
      }
    }
    if (count_ < kMax) {
      entries_[count_++] = {target_id, now_ms};
      return true;
    }
    int oldest = 0;
    for (int i = 1; i < kMax; ++i)
      if (entries_[i].last_ms < entries_[oldest].last_ms) oldest = i;
    entries_[oldest] = {target_id, now_ms};
    return true;
  }
  void Reset() { count_ = 0; }

 private:
  static constexpr int kMax = 16;
  struct Entry { int id = -1; long long last_ms = 0; };
  Entry entries_[kMax]{};
  int count_ = 0;
};

struct HapticPulse {
  float amplitude = 0.0f;  // 0..1, XrHapticVibration amplitude
  int duration_ms = 0;
};

// One native contact candidate: a stable per-target identity plus the
// sphere the solved blade is tested against. The native binding that fills
// these (enemy capsules) and applies damage is separate, traced work; this
// header owns only the fail-closed hit decision.
struct BladeContactTarget {
  int id = -1;
  Vec3 centre{0, 0, 0};
  float radius = 0.0f;
};

// Hit-quality -> controller pulse (QoL 22, scheduled with P5). Silent when
// haptics are disabled or the hit scaled to nothing. Thrusts are shorter
// and sharper than slashes at equal speed; amplitude follows the same
// threshold->full_speed ramp as damage so feel and numbers agree.
inline HapticPulse HitHaptic(float peak_speed, GestureKind kind,
                             float firing_threshold, bool haptics_enabled,
                             const CombatParams& p = {}) {
  if (!haptics_enabled) return {};
  float scale = DamageScale(peak_speed, kind, firing_threshold, 1.0f, p);
  if (scale <= 0.0f) return {};
  float span = p.full_speed - firing_threshold;
  float t = span > 1e-4f ? (peak_speed - firing_threshold) / span : 1.0f;
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  HapticPulse out;
  out.amplitude = 0.35f + 0.65f * t;
  out.duration_ms = kind == GestureKind::Thrust ? 45 : 80;
  return out;
}

// Blade segment from the committed native pose: the grip is the solved
// weapon bone's world origin (root * weapon), so the segment follows the
// rendered weapon through roomscale walking, torso turns and IK reach
// clamping instead of the raw controller pose. Direction is the tracked
// blade direction (XR grip-local -Z, hand_math.h) carried by the root
// basis only; the native blade axis and per-weapon blade length are not
// traced yet, so `blade_dir_world` and `length_m` remain caller-supplied
// assumptions, never read from the weapon basis. Failure leaves both
// outputs untouched.
inline bool BladeFromCommittedPose(const ArmPoseMatrix& root,
                                   const ArmPoseMatrix& weapon,
                                   Vec3 blade_dir_world, float length_m,
                                   Vec3& grip, Vec3& tip) {
  if (!arm_pose_detail::ValidMatrix(root) ||
      !arm_pose_detail::ValidMatrix(weapon) || !FiniteVec(blade_dir_world) ||
      !std::isfinite(length_m) || !(length_m > 0.0f))
    return false;
  ArmPoseMatrix basis = root;
  basis.m[3] = basis.m[7] = basis.m[11] = 0;
  const Vec3 dir = VNorm(ArmPoseTransformPoint(basis, blade_dir_world));
  if (VLen(dir) < 0.99f) return false;
  const Vec3 origin = ArmPoseTransformPoint(root, ArmPoseOrigin(weapon));
  if (!FiniteVec(origin)) return false;
  grip = origin;
  tip = VAdd(origin, VScale(dir, length_m));
  return FiniteVec(tip);
}

struct BladeHit {
  int target_id = -1;
  float damage_scale = 0.0f;
  // The measured tip speed that produced the scale. The damage candidate has
  // to carry the measurement, not just the multiplier derived from it, or a
   // later review cannot tell a fast graze from a slow push.
  float tip_speed_mps = 0.0f;
  HapticPulse haptic{};
  GestureKind kind = GestureKind::None;
};

// The H5 contact decision: tracked blade motion becomes at most one
// attributed hit. No overlap at speed, no damage; resting contact, lost
// tracking and sub-threshold motion fail closed. The gate is consulted
// only for overlapping targets, in caller order, so a swing crossing two
// enemies can hit each once. Applying `damage_scale` to a native actor
// (stamina/poise/reaction included) is the traced damage binding's job.
inline bool EvaluateBladeContact(const BladeKinematics& blade,
                                 GestureKind kind,
                                 std::span<const BladeContactTarget> targets,
                                 float contact_speed_min, float gain,
                                 int rehit_window_ms, long long now_ms,
                                 bool haptics_enabled, RehitGate& gate,
                                 BladeHit& out, const CombatParams& p = {}) {
  out = {};
  if (!blade.valid) return false;
  const float scale =
      DamageScaleContinuous(blade.tip_speed, kind, contact_speed_min, gain, p);
  if (scale <= 0.0f) return false;
  for (const auto& target : targets) {
    if (target.id < 0) continue;
    if (!BladeSweptOverlapsSphereArc(blade.prev_grip, blade.prev_tip,
                                     blade.grip, blade.tip, target.centre,
                                     target.radius, p))
      continue;
    if (!gate.TryAcquire(target.id, now_ms, rehit_window_ms)) continue;
    out.target_id = target.id;
    out.damage_scale = scale;
    out.tip_speed_mps = blade.tip_speed;
    out.kind = kind;
    out.haptic = HitHaptic(blade.tip_speed, kind, contact_speed_min,
                           haptics_enabled, p);
    return true;
  }
  return false;
}

}  // namespace dsvr
