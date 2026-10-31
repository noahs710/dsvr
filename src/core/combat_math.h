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

// Fraction along segment [a,b] nearest to p, 0 = a, 1 = b. Degenerate
// segments report 0. Non-finite input reports -1 (no grab point).
inline float BladeFraction(Vec3 a, Vec3 b, Vec3 p) {
  if (!FiniteVec(a) || !FiniteVec(b) || !FiniteVec(p)) return -1.0f;
  Vec3 ab{b.x - a.x, b.y - a.y, b.z - a.z};
  Vec3 ap{p.x - a.x, p.y - a.y, p.z - a.z};
  float len2 = VDot(ab, ab);
  if (!(len2 > 1e-12f)) return 0.0f;
  float t = VDot(ap, ab) / len2;
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  return t;
}

// Blade capsule (grip -> tip, blade_radius) vs sphere (centre, radius).
// Inclusive boundary counts as overlap: a grazing touch at speed is a hit
// the player felt connect; the damage scale already handles weakness.
// One capsule test: the segment [a,b] inflated by blade_radius_m against the
// sphere. Every blade-vs-sphere query shares this body so the validation rule
// (finite inputs, non-negative radii) can only be stated once.
inline bool BladeSegmentOverlapsSphere(Vec3 a, Vec3 b, Vec3 centre,
                                       float sphere_radius,
                                       const CombatParams& p) {
  if (!FiniteVec(a) || !FiniteVec(b) || !FiniteVec(centre) ||
      !(sphere_radius >= 0.0f) || !std::isfinite(p.blade_radius_m) ||
      p.blade_radius_m < 0.0f)
    return false;
  return SegmentPointDistance(a, b, centre) <= sphere_radius + p.blade_radius_m;
}

inline bool BladeOverlapsSphere(Vec3 grip, Vec3 tip, Vec3 centre,
                                float sphere_radius,
                                const CombatParams& p = {}) {
  return BladeSegmentOverlapsSphere(grip, tip, centre, sphere_radius, p);
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
  // Slice 1 halfsword: head-relative velocity of the support-hand grab point
  // on the committed blade segment. Fed by FeedGrabRelative alongside the
  // tip feed; the effective contact speed is measured at the grab point when
  // a grab is held, at the tip otherwise. Never derived by scaling tip speed.
  Vec3 grab{0, 0, 0};
  Vec3 prev_grab{0, 0, 0};
  Vec3 grab_velocity{0, 0, 0};
  float grab_speed = 0.0f;
  bool grab_valid = false;
  bool valid = false;
    bool velocity_sampled_ = false;
  Vec3 head{0, 0, 0};  // last head position (FeedRelative only)
  // Same-frame head velocity for the grab feed, before `head` advances.
  Vec3 head_velocity_raw_{};
  std::uint64_t space_epoch_ = 0;
  bool head_primed_ = false;

  void Reset() { *this = BladeKinematics{}; }
    bool HasVelocitySample() const { return velocity_sampled_; }

  // Support-hand grab uses the same coordinate frame and units as new_head.
  // Call after FeedRelative for that sample; use its captured head velocity,
  // not the head history that FeedRelative has already advanced. A bad grab
  // invalidates this channel only — the tip channel keeps its own history.
  void FeedGrabRelative(Vec3 new_grab, Vec3 new_head, float dt,
                        std::uint64_t space_epoch) {
    if (space_epoch != space_epoch_ || !head_primed_ || !FiniteVec(new_head) ||
        !FiniteVec(new_grab)) {
      grab_valid = false;
      grab_velocity = {0, 0, 0};
      grab_speed = 0.0f;
      return;
    }
    const Vec3 head_velocity_raw = head_velocity_raw_;
    if (!(dt > 0.0f) || dt > 0.25f || VLen(head_velocity_raw) > 25.0f) {
      grab_valid = false;
      grab_velocity = {0, 0, 0};
      grab_speed = 0.0f;
      return;
    }
    if (grab_valid) {
      const Vec3 raw = VScale(VAdd(new_grab, VScale(grab, -1)), 1 / dt);
      if (!FiniteVec(raw) || VLen(raw) > 25.0f) {
        grab = new_grab;
        prev_grab = new_grab;
        grab_velocity = {0, 0, 0};
        grab_speed = 0.0f;
        grab_valid = false;
        return;
      }
      const Vec3 relative = VAdd(raw, VScale(head_velocity_raw, -1.0f));
      grab_velocity = VAdd(VScale(grab_velocity, 0.5f), VScale(relative, 0.5f));
    }
    prev_grab = grab_valid ? grab : new_grab;
    grab = new_grab;
    grab_valid = true;
    grab_speed = VLen(grab_velocity);
  }

  // Effective contact point: the grab when held, the tip otherwise. The
  // caller supplies grab_held from the two-hand envelope; this only selects
  // which measured channel feeds the scale.
  float ContactSpeed(bool grab_held) const {
    if (grab_held && grab_valid) return grab_speed;
    return tip_speed;
  }

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
    velocity_sampled_ = false;
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
    velocity_sampled_ = advanced;
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
    head_velocity_raw_ = {};
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
    head_velocity_raw_ = head_velocity_raw;
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
  return BladeSegmentOverlapsSphere(grip, tip, centre, sphere_radius, p) ||
         BladeSegmentOverlapsSphere(prev_tip, tip, centre, sphere_radius, p);
}

// Continuous whole-blade query shared by target classification and PARAM slot
// qualification. Grip and length interpolate linearly; direction follows the
// shortest rotation. The distance to a fixed shape cannot decrease faster than
// |grip travel| + |length change| + max(length)*angle over this interval.
// Conservative advancement uses that bound, rather than a fixed number of
// poses or endpoint chords, so it also covers the middle of a translating blade.
// Contact tolerance is 1e-4 caller units (0.1 mm when supplied in metres).
// The query is bounded; ambiguous half-turns or exhausted work are Unresolved,
// never a certified Clear. Boolean adapters only accept Hit. This is source
// geometry, not proof of native hurt-volume, obstruction or submission ownership.
enum class BladeSweepResult { Invalid, Clear, Hit, Unresolved };
inline constexpr float kBladeSweepTolerance = 0.0001f;
inline constexpr int kBladeSweepIterations = 256;

template <class SegmentDistance>
inline BladeSweepResult SweepBladeContact(Vec3 prev_grip, Vec3 prev_tip,
                                          Vec3 grip, Vec3 tip, float reach,
                                          SegmentDistance distance) {
  if (!FiniteVec(prev_grip) || !FiniteVec(prev_tip) || !FiniteVec(grip) ||
      !FiniteVec(tip) || !std::isfinite(reach) || reach < 0)
    return BladeSweepResult::Invalid;
  const float start_distance = distance(prev_grip, prev_tip);
  const float end_distance = distance(grip, tip);
  if (!std::isfinite(start_distance) || start_distance < 0 ||
      !std::isfinite(end_distance) || end_distance < 0)
    return BladeSweepResult::Invalid;
  if (start_distance <= reach || end_distance <= reach)
    return BladeSweepResult::Hit;

  const Vec3 d0 = VAdd(prev_tip, VScale(prev_grip, -1));
  const Vec3 d1 = VAdd(tip, VScale(grip, -1));
  const Vec3 travel = VAdd(grip, VScale(prev_grip, -1));
  const float l0 = VLen(d0), l1 = VLen(d1);
  if (!std::isfinite(l0) || !std::isfinite(l1) ||
      l0 < 1e-6f || l1 < 1e-6f)
    return BladeSweepResult::Invalid;
  const Vec3 u0 = VScale(d0, 1 / l0), u1 = VScale(d1, 1 / l1);
  const float dot = std::fmax(-1.0f, std::fmin(1.0f, VDot(u0, u1)));
  if (dot < -0.9999f) return BladeSweepResult::Unresolved;
  const Vec3 normal = VCross(u0, u1);
  const float sine = VLen(normal);
  const float angle = std::atan2(sine, dot);
  const Vec3 tangent = sine > 1e-6f
      ? VCross(VScale(normal, 1 / sine), u0) : Vec3{};
  const double speed_bound = double(VLen(travel)) + std::fabs(double(l1) - l0) +
      double(std::fmax(l0, l1)) * angle;
  if (!std::isfinite(speed_bound)) return BladeSweepResult::Invalid;
  if (speed_bound == 0) return BladeSweepResult::Clear;

  double time = 0;
  float separation = start_distance;
  for (int iteration = 0; iteration < kBladeSweepIterations; ++iteration) {
    const double gap = double(separation) - reach;
    if (gap <= kBladeSweepTolerance) return BladeSweepResult::Hit;
    // This lower bound excludes the ENTIRE remaining interval, not just its
    // endpoint. Otherwise advance less than the earliest possible contact.
    if (gap > speed_bound * (1 - time)) return BladeSweepResult::Clear;
    time += 0.9 * gap / speed_bound;
    const float t = static_cast<float>(time);
    const Vec3 sample_grip = VAdd(prev_grip, VScale(travel, t));
    Vec3 sample_tip;
    if (sine > 1e-6f) {
      const Vec3 direction = VAdd(VScale(u0, std::cos(angle * t)),
                                  VScale(tangent, std::sin(angle * t)));
      sample_tip = VAdd(sample_grip, VScale(direction, l0 + (l1 - l0) * t));
    } else {
      sample_tip = VAdd(prev_tip, VScale(VAdd(tip, VScale(prev_tip, -1)), t));
    }
    if (!FiniteVec(sample_grip) || !FiniteVec(sample_tip))
      return BladeSweepResult::Invalid;
    separation = distance(sample_grip, sample_tip);
    if (!std::isfinite(separation) || separation < 0)
      return BladeSweepResult::Invalid;
  }
  return BladeSweepResult::Unresolved;
}

inline bool BladeSweptOverlapsSphereArc(Vec3 prev_grip, Vec3 prev_tip,
                                        Vec3 grip, Vec3 tip, Vec3 centre,
                                        float sphere_radius,
                                        const CombatParams& p = {}) {
  if (!FiniteVec(centre) || !std::isfinite(sphere_radius) || sphere_radius < 0 ||
      !std::isfinite(p.blade_radius_m) || p.blade_radius_m < 0.0f)
    return false;
  return SweepBladeContact(prev_grip, prev_tip, grip, tip,
      sphere_radius + p.blade_radius_m,
      [&](Vec3 a, Vec3 b) { return SegmentPointDistance(a, b, centre); }) ==
      BladeSweepResult::Hit;
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
// Shield guard plane from the committed native pose (slice 1 posture
  // guard): the shield face is the solved shield-hand origin carried by the
  // root basis, with the tracked grip-forward as its normal. Same snapshot
  // discipline as BladeFromCommittedPose — root * hand, never the raw
  // controller pose — so the guard follows the rendered body through
  // roomscale, torso turns and IK clamping. `face_radius_m` is the caller
  // assumption for the shield disc until native shield geometry is traced;
  // `native_units_per_meter` converts it to the plane's world-space units.
  // Failure leaves all outputs untouched.
  struct ShieldPlane {
    Vec3 centre{0, 0, 0};  // shield face centre, world space
    Vec3 normal{0, 0, 1};  // unit face normal, world space
    float radius = 0.0f;   // face disc radius in world units (caller assumption)
    bool valid = false;
  };
  inline bool ShieldPlaneFromCommittedPose(const ArmPoseMatrix& root,
                                        const ArmPoseMatrix& shield_hand,
                                        Vec3 face_normal_world,
                                        float face_radius_m,
                                        ShieldPlane& out,
                                        float native_units_per_meter = 1.0f) {
    if (!arm_pose_detail::ValidMatrix(root) ||
        !arm_pose_detail::ValidMatrix(shield_hand) ||
        !FiniteVec(face_normal_world) || !std::isfinite(face_radius_m) ||
        !(face_radius_m > 0.0f) ||
        !std::isfinite(native_units_per_meter) ||
        !(native_units_per_meter > 0.0f)) {
      out = {};
      return false;
    }
    ArmPoseMatrix basis = root;
    basis.m[3] = basis.m[7] = basis.m[11] = 0;
    const Vec3 n = VNorm(ArmPoseTransformPoint(basis, face_normal_world));
    if (VLen(n) < 0.99f) {
      out = {};
      return false;
    }
    const Vec3 centre =
        ArmPoseTransformPoint(root, ArmPoseOrigin(shield_hand));
    if (!FiniteVec(centre)) {
      out = {};
      return false;
    }
    out.centre = centre;
    out.normal = n;
    out.radius = face_radius_m * native_units_per_meter;
    out.valid = true;
    return true;
  }

  // Threat vs shield plane: segment-vs-disc coverage. The threat segment
  // (committed weapon blade) must cross the shield disc plane within the disc
  // radius. A hand-sphere proxy is forbidden here: coverage is the plane the
  // committed pose actually holds, not a ball around the hand.
  // Slice 5 rim: a threat striking the disc rim (within rim_m of the edge)
  // still counts as a block — a glancing edge hit is a deflect, not a miss.
  // rim_m is physical metres and is converted using native_units_per_meter.
  // rim_m <= 0 disables the rim (face only). Non-finite rim refuses.
  inline bool ThreatBlockedByShield(Vec3 threat_grip, Vec3 threat_tip,
                                  const ShieldPlane& shield,
                                  float rim_m = 0.0f,
                                  float native_units_per_meter = 1.0f) {
    if (!shield.valid || !FiniteVec(threat_grip) || !FiniteVec(threat_tip) ||
        !FiniteVec(shield.centre) || !FiniteVec(shield.normal) ||
        !(shield.radius > 0.0f) || !std::isfinite(rim_m) || rim_m < 0.0f ||
        !std::isfinite(native_units_per_meter) ||
        !(native_units_per_meter > 0.0f))
      return false;
    const float reach = shield.radius + rim_m * native_units_per_meter;
    const Vec3 seg{threat_tip.x - threat_grip.x, threat_tip.y - threat_grip.y,
                   threat_tip.z - threat_grip.z};
    const float denom = VDot(seg, shield.normal);
    if (std::fabs(denom) < 1e-6f) {
      // Parallel: only a segment already lying in the disc plane counts.
      if (std::fabs(VDot(VAdd(threat_grip, VScale(shield.centre, -1)),
                         shield.normal)) > reach)
        return false;
      return SegmentPointDistance(threat_grip, threat_tip, shield.centre) <=
             reach;
    }
    const float t = VDot(VAdd(shield.centre, VScale(threat_grip, -1)),
                         shield.normal) /
                    denom;
    if (t < 0.0f || t > 1.0f) return false;
    const Vec3 hit{threat_grip.x + seg.x * t, threat_grip.y + seg.y * t,
                   threat_grip.z + seg.z * t};
    return VLen(VAdd(hit, VScale(shield.centre, -1))) <= reach;
  }

  // Deflect pulse (slice 1): shorter and sharper than the thrust pulse, fired
  // only from classified bench geometry (a block that interrupts a swing at
  // speed). Amplitude and duration are caller-supplied (deflect_amplitude /
  // deflect_duration_ms config); non-finite or out-of-range inputs refuse.
  inline HapticPulse DeflectHaptic(float amplitude, int duration_ms,
                                   bool haptics_enabled) {
    if (!haptics_enabled || !std::isfinite(amplitude) || amplitude <= 0.0f ||
        amplitude > 1.0f || duration_ms <= 0)
      return {};
    return {amplitude, duration_ms};
  }

  // Slice 4 two-hand torque: a support hand riding the blade steadies the
  // swing, so the same hand motion carries more committed energy. `grabbed`
  // selects the two-hand rate; `grab_fraction` (0 = hilt, 1 = tip) scales it:
  // a hilt grab commits the full blade, a mid-blade halfsword grab shortens
  // the lever and commits less. Returns the energy multiplier (>= 1 when
  // grabbed, exactly 1 otherwise). Non-finite fraction refuses to 1.
  inline float TwoHandTorque(bool grabbed, float grab_fraction) {
    if (!grabbed) return 1.0f;
    if (!std::isfinite(grab_fraction)) return 1.0f;
    float f = grab_fraction;
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    // Hilt grab 1.25x, fading to 1.0x at the tip (no lever left to steady).
    return 1.25f - 0.25f * f;
  }

  // Two-hand attach/detach tick: a short quiet pulse fired once when the
  // support grip attaches or releases. Same shape as the deflect pulse
  // (caller-supplied amplitude/duration); attach and detach share it.
  inline HapticPulse GripTick(float amplitude, int duration_ms,
                              bool haptics_enabled) {
    return DeflectHaptic(amplitude, duration_ms, haptics_enabled);
  }

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

// Slice 7 controller aim ray (geometry only, no native binding): the
// spell/bow aim direction from the independent XR aim pose (HandSample
// aim_pos/aim_quat, input.h). Grip-local -Z convention, same as the blade.
// Pure function on the aim fields; the caller checks aim_valid, not grip
// valid. XR LOCAL space — never mix with committed model-space hands
// without an explicit transform. No native submission, no target binding.
struct AimRay {
  Vec3 origin{0, 0, 0};
  Vec3 dir{0, 0, -1};
  bool valid = false;
};
inline bool AimRayFromAimPose(const float pos[3], const float quat[4],
                              AimRay& out) {
  out = {};
  if (!pos || !quat) return false;
  const Vec3 p{pos[0], pos[1], pos[2]};
  if (!FiniteVec(p)) return false;
  const float n2 = quat[0] * quat[0] + quat[1] * quat[1] +
                   quat[2] * quat[2] + quat[3] * quat[3];
  if (!(n2 > 0.25f) || !(n2 < 2.25f)) return false;
  const Vec3 d = QuatRotate({0, 0, -1}, quat[0], quat[1], quat[2], quat[3]);
  if (!FiniteVec(d) || VLen(d) < 1e-6f) return false;
  out.origin = p;
  out.dir = VScale(d, 1.0f / VLen(d));
  out.valid = true;
  return true;
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
                                 BladeHit& out, const CombatParams& p = {},
                                   float torque_response = 1.0f) {
  out = {};
  if (!blade.valid) return false;
  // Slice 2 kinetic scale: torque gates energy, energy gates scale. The
  // response (per-weapon rate, 1 = transparent) shapes how fast the measured
  // speed rises; the energy gate then decides the scale. Weight never edits
  // damage directly. Non-positive/non-finite response refuses (no scale from
  // a mis-tuned table), it never falls back to raw speed silently.
  float energy = blade.tip_speed;
  if (torque_response != 1.0f) {
    if (!std::isfinite(torque_response) || torque_response <= 0.0f) return false;
    energy = blade.tip_speed * torque_response;
    if (!std::isfinite(energy) || energy < 0.0f) return false;
  }
  const float scale =
      DamageScaleContinuous(energy, kind, contact_speed_min, gain, p);
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
    // Iteration 3: report and feel the gated energy, not the raw speed.
    // Scale, tip_speed_mps and haptic must agree or a heavy weapon shows
    // a fast number with a weak hit (or the reverse).
      out.tip_speed_mps = energy;
      out.kind = kind;
      out.haptic = HitHaptic(energy, kind, contact_speed_min,
                           haptics_enabled, p);
    return true;
  }
  return false;
}

}  // namespace dsvr
