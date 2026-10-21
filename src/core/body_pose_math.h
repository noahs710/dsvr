#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>

#include "arm_pose_math.h"

namespace dsvr {

// Anatomical torso twist: the chest may follow the head only this far from
// the body heading. Larger head turns are the heading system's job (right
// stick turns, recenter, physical roomscale turning), not spine wind-up.
// Native animation remains the gait source; the post-writer leg pass only
// counterposes feet from measured root motion. The torso solver never translates
// the pelvis/root, so stick-walking cannot drag the chest ahead of the user.
inline constexpr float kTorsoTwistLimitRad = 0.8f;
// Hard bound on one frame's torso correction, equal to the twist bound:
// the torso may not wind further from the native facing than it may from
// the heading. A larger gap is locomotion facing, not a head turn, and
// chasing it rotated the chest across the camera in live testing.
// Native poses are rebuilt every frame, so this cannot accumulate.
inline constexpr float kTorsoCorrectionLimitRad = 0.8f;
// Physical head-displacement budget for the inferred torso solve. The spine
// root stays fixed; joint rotations carry the neck and shoulders without stretch.
inline constexpr float kTorsoLeanLimitM = 0.30f;

inline float WrapAngleRad(float angle) {
  if (!std::isfinite(angle)) return 0.0f;
  return std::remainder(angle, 6.283185307179586f);
}

// Signed yaw of `forward` in a body frame, matching the camera convention:
// positive toward +right (head_yaw in CameraApplyFirstPerson). A forward
// with no planar component yields 0 rather than a NaN.
inline float RelativeYaw(Vec3 forward, Vec3 right, Vec3 fwd) {
  return std::atan2(VDot(forward, right), VDot(forward, fwd));
}

// Keeps a root's yaw continuous across the branch cut. This is deliberately
// separate from matrix orientation: matrices remain periodic while motion
// estimates can distinguish repeated full turns.
class ContinuousYaw {
 public:
  float Update(float angle_rad) {
    if (!std::isfinite(angle_rad)) return value_;
    const float wrapped = WrapAngleRad(angle_rad);
    if (!primed_) {
      primed_ = true;
      previous_ = wrapped;
      value_ = wrapped;
    } else {
      value_ += WrapAngleRad(wrapped - previous_);
      previous_ = wrapped;
    }
    return value_;
  }
  void Reset() { primed_ = false; previous_ = value_ = 0; }
  bool Primed() const { return primed_; }
  float Value() const { return value_; }

 private:
  bool primed_ = false;
  float previous_ = 0, value_ = 0;
};

struct BodyMotionEstimate {
  bool valid = false;
  Vec3 displacement{}, velocity{};
  float yaw = 0, yaw_delta = 0;
};

// Estimates player motion from successive native model-root transforms. Long
// gaps and root jumps re-prime instead of turning a teleport into a foot target.
class BodyMotionEstimator {
 public:
  BodyMotionEstimate Update(Vec3 root_origin, float root_yaw,
                            std::uint64_t now_ms) {
    BodyMotionEstimate out{};
    if (!FiniteVec(root_origin) || !std::isfinite(root_yaw)) return out;
    const float wrapped = WrapAngleRad(root_yaw);
    if (primed_ && now_ms <= previous_ms_) {
      out.yaw = yaw_.Value();
      return out;
    }
    out.yaw = yaw_.Update(wrapped);
    if (!primed_) {
      Prime(root_origin, wrapped, now_ms);
      out.yaw = yaw_.Value();
      return out;
    }
    const std::uint64_t dt_ms = now_ms - previous_ms_;
    const Vec3 displacement = arm_pose_detail::Sub(root_origin, previous_origin_);
    if (dt_ms > 250 || !std::isfinite(VLen(displacement)) ||
        VLen(displacement) > 1.0f) {
      Reset();
      Prime(root_origin, wrapped, now_ms);
      out.yaw = yaw_.Value();
      return out;
    }
    out.valid = true;
    out.displacement = displacement;
    out.velocity = VScale(displacement, 1000.0f / static_cast<float>(dt_ms));
    out.yaw_delta = WrapAngleRad(wrapped - previous_yaw_);
    previous_origin_ = root_origin;
    previous_yaw_ = wrapped;
    previous_ms_ = now_ms;
    return out;
  }
  void Reset() {
    primed_ = false;
    previous_origin_ = {};
    previous_yaw_ = 0;
    previous_ms_ = 0;
    yaw_.Reset();
  }

 private:
  void Prime(Vec3 origin, float yaw, std::uint64_t now_ms) {
    primed_ = true;
    previous_origin_ = origin;
    previous_yaw_ = yaw;
    previous_ms_ = now_ms;
    yaw_.Reset();
    yaw_.Update(yaw);
  }
  bool primed_ = false;
  Vec3 previous_origin_{};
  float previous_yaw_ = 0;
  std::uint64_t previous_ms_ = 0;
  ContinuousYaw yaw_;
};

struct BodyLegBones {
  int thigh = -1, calf = -1, foot = -1, calf_twist = -1;
};
struct BodyLegCalibration {
  bool valid = false;
  float upper_length = 0, lower_length = 0;
};

inline bool CalibrateBodyLeg(std::span<const ArmPoseMatrix> poses,
                             std::span<const int> parents,
                             const BodyLegBones& bones,
                             BodyLegCalibration& calibration) {
  const auto count = poses.size();
  const auto valid_index = [count](int bone) {
    return bone >= 0 && static_cast<std::size_t>(bone) < count;
  };
  if (!count || count != parents.size() || !valid_index(bones.thigh) ||
      !valid_index(bones.calf) || !valid_index(bones.foot) ||
      bones.thigh == bones.calf || bones.calf == bones.foot ||
      bones.thigh == bones.foot || parents[bones.thigh] < 0 ||
      parents[bones.calf] != bones.thigh || parents[bones.foot] != bones.calf ||
      bones.calf_twist < -1 ||
      (bones.calf_twist >= 0 && (!valid_index(bones.calf_twist) ||
       bones.calf_twist == bones.thigh || bones.calf_twist == bones.calf ||
       bones.calf_twist == bones.foot || parents[bones.calf_twist] != bones.thigh)) ||
      !arm_pose_detail::ValidMatrix(poses[bones.thigh]) ||
      !arm_pose_detail::ValidMatrix(poses[bones.calf]) ||
      !arm_pose_detail::ValidMatrix(poses[bones.foot])) return false;
  const float upper = VLen(arm_pose_detail::Sub(ArmPoseOrigin(poses[bones.calf]),
                                                ArmPoseOrigin(poses[bones.thigh])));
  const float lower = VLen(arm_pose_detail::Sub(ArmPoseOrigin(poses[bones.foot]),
                                                ArmPoseOrigin(poses[bones.calf])));
  if (!std::isfinite(upper) || !std::isfinite(lower) ||
      !(upper > 1e-4f) || !(lower > 1e-4f)) return false;
  calibration = {true, upper, lower};
  return true;
}

inline ArmPoseResult SolveBodyLegPose(std::span<ArmPoseMatrix> poses,
                                      std::span<const int> parents,
                                      const BodyLegBones& bones,
                                      const BodyLegCalibration& calibration,
                                      Vec3 foot_target,
                                      const Vec3* preferred_knee_pole = nullptr) {
  if (!calibration.valid) return {};
  const ArmPoseLengths lengths{calibration.upper_length,
                               calibration.lower_length};
  const ArmPoseBones chain{bones.thigh, bones.calf, bones.foot,
                           -1, -1, bones.calf_twist};
  return SolveModelArmPose(poses, parents, chain, foot_target,
                           preferred_knee_pole, &lengths);
}

// Counter root translation/turn so the animation's foot target is expressed
// in the estimated player's stable body frame. The foot basis itself is kept
// by the limb solver, preserving the animation's natural ankle orientation.
inline bool BodyFootTarget(Vec3 native_foot, Vec3 pelvis,
                           Vec3 root_displacement_model, float root_yaw_delta,
                           Vec3& target) {
  if (!FiniteVec(native_foot) || !FiniteVec(pelvis) ||
      !FiniteVec(root_displacement_model) || !std::isfinite(root_yaw_delta))
    return false;
  const Vec3 offset = arm_pose_detail::Sub(native_foot, pelvis);
  const auto turn = arm_pose_detail::AxisRotation({0, 1, 0}, -root_yaw_delta);
  const Vec3 candidate = arm_pose_detail::Sub(
      VAdd(pelvis, turn.Apply(offset)), root_displacement_model);
  if (!FiniteVec(candidate)) return false;
  target = candidate;
  return true;
}

// Rigidly rotate every pose in `bone`'s subtree about a model-space axis
// through `pivot`. Segment lengths and bone scales survive exactly; bones
// outside the subtree stay bit-identical. Malformed hierarchies, bases or
// transform inputs refuse with the whole palette unchanged.
inline bool RotatePoseSubtree(std::span<ArmPoseMatrix> poses,
                              std::span<const int> parents, int bone,
                              Vec3 axis, Vec3 pivot, float angle_rad) {
  using namespace arm_pose_detail;
  const std::size_t count = poses.size();
  if (count == 0 || count != parents.size() ||
      count > static_cast<std::size_t>((std::numeric_limits<int>::max)()) ||
      bone < 0 || static_cast<std::size_t>(bone) >= count ||
      !FiniteVec(pivot) || !FiniteVec(axis) ||
      !std::isfinite(angle_rad))
    return false;
  const Vec3 unit_axis = VNorm(axis);
  if (VLen(unit_axis) < .99f) return false;
  for (std::size_t i = 0; i < count; ++i) {
    for (float value : poses[i].m)
      if (!std::isfinite(value)) return false;
    if (parents[i] < -1 ||
        (parents[i] >= 0 && static_cast<std::size_t>(parents[i]) >= count))
      return false;
  }
  for (std::size_t i = 0; i < count; ++i) {
    int current = static_cast<int>(i);
    std::size_t steps = 0;
    while (current != -1 && steps++ < count) current = parents[current];
    if (current != -1) return false;  // parent cycle
  }
  const Rotation rotation = AxisRotation(unit_axis, angle_rad);
  for (std::size_t i = 0; i < count; ++i) {
    if (!InSubtree(static_cast<int>(i), bone, parents)) continue;
    if (!ValidMatrix(poses[i]) || !ValidMatrix(Rotate(poses[i], rotation, pivot)))
      return false;
  }
  for (std::size_t i = 0; i < count; ++i)
    if (InSubtree(static_cast<int>(i), bone, parents))
      poses[i] = Rotate(poses[i], rotation, pivot);
  return true;
}

// Turn the spine subtree so the torso faces the body heading plus the
// head's clamped yaw offset, instead of the native animation facing. This
// is what lets the shoulders (and therefore both arm chains solved from
// them) follow head movement; without it the wrist targets rotate with
// the VR heading while the shoulders stay on the animated torso and the
// arms cross or run out of reach.
//
// `world_from_model` is the model root (translation ignored). Heading and
// head forwards are world-space. On success `applied_yaw_rad` receives the
// world-frame correction; a correction below a milliradian-equivalent
// threshold is a successful no-op. Failure leaves all poses unchanged.
inline bool SolveTorsoYaw(std::span<ArmPoseMatrix> poses,
                          std::span<const int> parents,int spine_bone,
                          const ArmPoseMatrix& world_from_model,
                          Vec3 heading_right,Vec3 heading_fwd,Vec3 head_fwd,
                          float& applied_yaw_rad) {
  applied_yaw_rad=0;
  if (poses.empty() || poses.size()!=parents.size() || spine_bone<0 ||
      static_cast<std::size_t>(spine_bone)>=poses.size() ||
      !FiniteVec(heading_right) || !FiniteVec(heading_fwd) || !FiniteVec(head_fwd)) return false;
  ArmPoseMatrix root=world_from_model;root.m[3]=root.m[7]=root.m[11]=0;
  if (!arm_pose_detail::ValidMatrix(root)) return false;
  const Vec3 right_w=VNorm(VScale(arm_pose_detail::Column(root,0),-1));
  const Vec3 facing_w=VCross(right_w,VNorm(arm_pose_detail::Column(root,1)));
  const Vec3 hr=VNorm(heading_right),hf=VNorm(heading_fwd),head=VNorm(head_fwd);
  if (VLen(hr)<.99f || VLen(hf)<.99f || VLen(head)<.99f ||
      std::fabs(VDot(hr,hf))>1e-3f) return false;
  const float native_rel=RelativeYaw(facing_w,hr,hf);
  const float desired=std::clamp(RelativeYaw(head,hr,hf),-kTorsoTwistLimitRad,kTorsoTwistLimitRad);
  const float delta=std::clamp(WrapAngleRad(desired-native_rel),
                              -kTorsoCorrectionLimitRad,kTorsoCorrectionLimitRad);
  if (std::fabs(delta)<1e-4f) return true;
  const float limited=native_rel+delta;
  Vec3 up_m{},facing_m{},desired_m{};
  if (!ArmPoseInversePoint(root,{0,1,0},up_m) ||
      !ArmPoseInversePoint(root,facing_w,facing_m) ||
      !ArmPoseInversePoint(root,VAdd(VScale(hf,std::cos(limited)),VScale(hr,std::sin(limited))),desired_m)) return false;
  up_m=VNorm(up_m);
  const Vec3 a=arm_pose_detail::Sub(facing_m,VScale(up_m,VDot(facing_m,up_m)));
  const Vec3 b=arm_pose_detail::Sub(desired_m,VScale(up_m,VDot(desired_m,up_m)));
  if (VLen(up_m)<.99f || VLen(a)<1e-4f || VLen(b)<1e-4f) return false;
  const float angle=std::atan2(VDot(VCross(VNorm(a),VNorm(b)),up_m),VDot(VNorm(a),VNorm(b)));
  if (!RotatePoseSubtree(poses,parents,spine_bone,up_m,ArmPoseOrigin(poses[spine_bone]),angle)) return false;
  applied_yaw_rad=delta;return true;
}

// Lean uses the authored spine/chest/neck chain and the shared two-bone solver.
// No pelvis/root translation or measured-eye/head-pivot equivalence is assumed.
// Bones outside the spine subtree stay bit-identical; refusal is atomic.
// `applied_m` reports actual neck displacement in physical meters.
// The lean target is the tracked head displacement from the calibrated
// upright stance: walking moves the anchor and the head together, so the
// difference stays near zero and the torso never "outruns" the user.
// Leaning bends the spine toward the head, and the head-rider in arm_ik
// keeps the neck joined to the viewpoint (no visible gap).
struct TorsoBones {
  int pelvis=-1,spine=-1,chest=-1,neck=-1,head=-1;
  // FLVER may place anatomical Pelvis beside Upper_Root->Spine.
  int torso_root=-1;
};
namespace torso_detail {
// DSR's selected native palette has 80 slots; scratch storage stays on the stack.
inline constexpr std::size_t kPoseCapacity=80;
inline bool ValidRig(std::span<const ArmPoseMatrix> poses,std::span<const int> parents,
                     const TorsoBones& bones) {
  const std::size_t count=poses.size();
  const auto valid=[&](int bone) {return bone>=0 && static_cast<std::size_t>(bone)<count;};
  if (!count || count > kPoseCapacity || count!=parents.size() ||
      !valid(bones.spine) || !valid(bones.chest) || !valid(bones.neck) ||
      !valid(bones.head) || bones.pelvis < -1 ||
      (bones.pelvis>=0 && !valid(bones.pelvis)) || bones.torso_root < -1 ||
      (bones.torso_root>=0 && !valid(bones.torso_root))) return false;
  for (std::size_t i=0;i<count;++i) {
    if (parents[i]<-1 || (parents[i]>=0 && !valid(parents[i]))) return false;
    for (float v:poses[i].m) if (!std::isfinite(v)) return false;
  }
  for (std::size_t i=0;i<count;++i) {
    int bone=static_cast<int>(i);std::size_t steps=0;
    while (bone!=-1 && steps++<count) bone=parents[bone];
    if (bone!=-1) return false;
  }
  if (parents[bones.chest]!=bones.spine || parents[bones.neck]!=bones.chest ||
      parents[bones.head]!=bones.neck || parents[bones.spine]<0 ||
      (bones.torso_root>=0 && parents[bones.spine]!=bones.torso_root) ||
      (bones.torso_root<0 && bones.pelvis>=0 && parents[bones.spine]!=bones.pelvis)) return false;
  const int subtree=bones.pelvis>=0?bones.pelvis:bones.spine;
  for (std::size_t i=0;i<count;++i)
    if (arm_pose_detail::InSubtree(static_cast<int>(i),subtree,parents) &&
        !arm_pose_detail::ValidMatrix(poses[i])) return false;
  return true;
}
}
inline bool SolveTorsoLean(std::span<ArmPoseMatrix> poses,
                           std::span<const int> parents,const TorsoBones& bones,
                           const ArmPoseMatrix& world_from_model,
                           Vec3 lean_world,float& applied_m,float world_units_per_meter=1.f) {
  using namespace arm_pose_detail;
  applied_m=0;
  if (!torso_detail::ValidRig(poses,parents,bones) || !FiniteVec(lean_world) ||
      !std::isfinite(world_units_per_meter) || world_units_per_meter<=0) return false;
  ArmPoseMatrix root=world_from_model;root.m[3]=root.m[7]=root.m[11]=0;
  if (!ValidMatrix(root)) return false;
  const float distance=VLen(lean_world);
  if (!std::isfinite(distance)) return false;
  if (distance<1e-5f) return true;
  const float limit_world=kTorsoLeanLimitM*world_units_per_meter;
  if (distance>limit_world) lean_world=VScale(lean_world,limit_world/distance);
  Vec3 offset{};
  if (!ArmPoseInversePoint(root,lean_world,offset)) return false;
  const Vec3 spine=ArmPoseOrigin(poses[bones.spine]);
  const Vec3 chest=ArmPoseOrigin(poses[bones.chest]);
  const Vec3 neck=ArmPoseOrigin(poses[bones.neck]);
  const Vec3 native=Sub(neck,spine);
  const float upper=VLen(Sub(chest,spine)),lower=VLen(Sub(neck,chest));
  if (!(upper>1e-4f && lower>1e-4f)) return false;
  Vec3 target=VAdd(native,offset);
  const float length=VLen(target),native_length=VLen(native);
  if (!(length>1e-5f && native_length>1e-5f)) return false;
  Vec3 direction=VNorm(target);
  const Vec3 native_direction=VNorm(native);
  constexpr float limit=.6f;
  const float swing=std::acos(std::clamp(VDot(native_direction,direction),-1.f,1.f));
  if (swing>limit) {
    const Vec3 axis=VNorm(VCross(native_direction,direction));
    if (VLen(axis)<.99f) return false;
    direction=AxisRotation(axis,limit).Apply(native_direction);
  }
  // Bound inferred spine flexion; crouching cannot collapse an untracked pelvis.
  const float minimum=std::min(native_length,std::sqrt(upper*upper+lower*lower+
                                                       2*upper*lower*std::cos(limit)));
  target=VAdd(spine,VScale(direction,std::clamp(length,minimum,upper+lower)));
  const Vec3 pole=VScale(Column(poses[bones.spine],2),-1);
  const ArmPoseBones chain{bones.spine,bones.chest,bones.neck};
  const auto solved=SolveModelArmPose(poses,parents,chain,target,&pole);
  if (!solved.valid) return false;
  applied_m=VLen(ArmPoseTransformPoint(root,Sub(ArmPoseOrigin(poses[bones.neck]),neck)))/world_units_per_meter;
  return true;
}

// ---- Solver pass: distributed torso, pelvis share, head snap, damping ----

inline constexpr float kPelvisYawShare = 0.5f;
inline constexpr float kPelvisYawCapRad = 0.40f;
inline constexpr float kHeadSnapCapRad = 0.60f;
inline constexpr float kLeanPitchDeadbandRad = 0.15f;
inline constexpr float kLeanPitchFactor = 0.35f;
inline constexpr float kLeanPitchCapRad = 0.30f;
// Torso yaw follows the head 1:1 through DampedAngle (90 ms tau): the
// chest tracks the head with no lag the user can feel, and the deadband
// above keeps small glances from moving the body at all. Slower damping
// here read as the body "outrunning" or trailing the user in live tests.
inline constexpr float kTorsoDampTauMs = 90.0f;
inline constexpr float kTorsoDampDeadbandRad = 0.003f;
inline constexpr float kTorsoDampDeadbandM = 0.002f;

// Split a total joint rotation across three joints by weight, clamped to
// per-joint caps. Any amount a capped joint cannot take is redistributed
// once to the uncapped joints in the same ratio; the rest is dropped.
inline std::array<float, 3> DistributeJointAngles(float total,
                                                  const float weights[3],
                                                  const float caps[3]) {
  std::array<float, 3> out{0, 0, 0};
  if (!std::isfinite(total)) return out;
  float wsum = 0;
  for (int i = 0; i < 3; ++i) {
    if (!std::isfinite(weights[i]) || !std::isfinite(caps[i]) || caps[i] < 0)
      return out;
    wsum += weights[i] > 0 ? weights[i] : 0;
  }
  if (wsum <= 0) return out;
  const float sign = total < 0 ? -1.0f : 1.0f;
  float remaining = std::fabs(total);
  bool capped[3]{};
  for (int pass = 0; pass < 2 && remaining > 1e-6f; ++pass) {
    float open_weight = 0;
    for (int i = 0; i < 3; ++i)
      if (!capped[i]) open_weight += weights[i] > 0 ? weights[i] : 0;
    if (open_weight <= 0) break;
    for (int i = 0; i < 3; ++i) {
      if (capped[i] || weights[i] <= 0) continue;
      const float room = caps[i] - std::fabs(out[i]);
      const float share = remaining * weights[i] / open_weight;
      const float take = share < room ? share : room;
      out[i] += sign * take;
      if (take >= room - 1e-6f) capped[i] = true;
    }
    float used = 0;
    for (float v : out) used += std::fabs(v);
    remaining = std::fabs(total) - used;
  }
  return out;
}

// Desired torso yaw offset from the heading: the head-driven yaw, with a
// small deadband so looking slightly left/right never spins the body.
// Head-only by design: live reports show the torso outrunning the user
// when the hands drag the chest.
inline float TorsoTargetYaw(Vec3 head_fwd,Vec3 heading_right,
                            Vec3 heading_fwd) {
  if (!FiniteVec(head_fwd) || !FiniteVec(heading_right) || !FiniteVec(heading_fwd)) return 0;
  const Vec3 hr=VNorm(heading_right),hf=VNorm(heading_fwd),head_f=VNorm(head_fwd);
  if (VLen(hr)<.99f || VLen(hf)<.99f || VLen(head_f)<.99f ||
      std::fabs(VDot(hr,hf))>1e-3f) return 0;
  const float head=RelativeYaw(head_f,hr,hf);
  // Deadband: small glances stay head-only, no torso motion at all.
  // The band recenters the response (no jump at the edge) and the result
  // is clamped to the anatomical twist bound.
  constexpr float kTorsoLookDeadbandRad = 0.12f;
  const float magnitude = std::fabs(head);
  if (magnitude <= kTorsoLookDeadbandRad) return 0.0f;
  const float sign = head > 0.0f ? 1.0f : -1.0f;
  const float rescaled = (magnitude - kTorsoLookDeadbandRad) /
      (kTorsoTwistLimitRad - kTorsoLookDeadbandRad) * kTorsoTwistLimitRad;
  return std::clamp(sign * rescaled,
                    -kTorsoTwistLimitRad,kTorsoTwistLimitRad);
}

// Signed spine pitch share for head pitch beyond the deadband (positive
// looking up). The caller distributes it Spine:Chest 60:40.
inline float TorsoPitchShare(Vec3 head_fwd) {
  if (!FiniteVec(head_fwd) || VLen(head_fwd) < 1e-4f) return 0.0f;
  const float y = std::clamp(VNorm(head_fwd).y, -1.0f, 1.0f);
  const float pitch = std::asin(y);
  const float excess = pitch - std::clamp(pitch, -kLeanPitchDeadbandRad,
                                          kLeanPitchDeadbandRad);
  return std::clamp(kLeanPitchFactor * excess, -kLeanPitchCapRad,
                    kLeanPitchCapRad);
}


struct TorsoPoseResult {
  bool valid = false;
  float applied_delta = 0, pelvis_applied = 0, snap_applied = 0;
};

namespace torso_detail {
inline bool RootFrame(const ArmPoseMatrix& world_from_model, Vec3& right_w,
                      Vec3& up_w, Vec3& facing_w, ArmPoseMatrix& root) {
  root = world_from_model;
  root.m[3] = root.m[7] = root.m[11] = 0;
  if (!arm_pose_detail::ValidMatrix(root)) return false;
  right_w = VNorm(arm_pose_detail::Sub(Vec3{},
                                       arm_pose_detail::Column(root, 0)));
  up_w = VNorm(arm_pose_detail::Column(root, 1));
  facing_w = VCross(right_w, up_w);
  return VLen(right_w) >= .99f && VLen(up_w) >= .99f && VLen(facing_w) >= .99f;
}
// Signed model-space angle about model up taking facing_w to desired_w.
inline bool ModelYawAngle(const ArmPoseMatrix& root, Vec3 facing_w,
                          Vec3 desired_w, Vec3& up_m, float& angle) {
  Vec3 facing_m{}, desired_m{};
  if (!ArmPoseInversePoint(root, {0, 1, 0}, up_m) ||
      !ArmPoseInversePoint(root, facing_w, facing_m) ||
      !ArmPoseInversePoint(root, desired_w, desired_m))
    return false;
  up_m = VNorm(up_m);
  facing_m = VNorm(facing_m);
  desired_m = VNorm(desired_m);
  if (VLen(up_m) < .99f || VLen(facing_m) < .99f || VLen(desired_m) < .99f)
    return false;
  const Vec3 a = arm_pose_detail::Sub(facing_m,
                                      VScale(up_m, VDot(facing_m, up_m)));
  const Vec3 b = arm_pose_detail::Sub(desired_m,
                                      VScale(up_m, VDot(desired_m, up_m)));
  if (VLen(a) < 1e-4f || VLen(b) < 1e-4f) return false;
  angle = std::atan2(VDot(VCross(VNorm(a), VNorm(b)), up_m),
                     VDot(VNorm(a), VNorm(b)));
  return std::isfinite(angle);
}
}  // namespace torso_detail

// Full torso solver pass: pelvis share, distributed spine/chest/neck yaw,
// lean pitch, and a neck-capped head snap aligning the head bone's facing
// and up to the camera. `desired_yaw` is the (already damped) target
// offset from the heading; `pitch_share` comes from TorsoPitchShare.
// All stages solve in bounded scratch storage. Refusal leaves the complete
// input palette unchanged and result.valid false.
inline bool SolveTorsoPose(std::span<ArmPoseMatrix> poses,
                           std::span<const int> parents,const TorsoBones& bones,
                           const ArmPoseMatrix& world_from_model,
                           Vec3 heading_right,Vec3 heading_fwd,float desired_yaw,float pitch_share,
                           Vec3 cam_right,Vec3 cam_up,Vec3 cam_fwd,TorsoPoseResult& result) {
  result={};
  if (!torso_detail::ValidRig(poses,parents,bones) ||
      !FiniteVec(heading_right) || !FiniteVec(heading_fwd) ||
      !FiniteVec(cam_right) || !FiniteVec(cam_up) || !FiniteVec(cam_fwd) ||
      !std::isfinite(desired_yaw) || !std::isfinite(pitch_share)) return false;
  const Vec3 hr=VNorm(heading_right),hf=VNorm(heading_fwd);
  const Vec3 cr=VNorm(cam_right),cu=VNorm(cam_up),cf=VNorm(cam_fwd);
  if (VLen(hr)<.99f || VLen(hf)<.99f || std::fabs(VDot(hr,hf))>1e-3f ||
      VLen(cr)<.99f || VLen(cu)<.99f || VLen(cf)<.99f ||
      std::fabs(VDot(cr,cu))>1e-3f || std::fabs(VDot(cr,cf))>1e-3f ||
      std::fabs(VDot(cu,cf))>1e-3f || VDot(VCross(cr,cu),cf)<.999f) return false;
  ArmPoseMatrix root{};Vec3 right_w{},up_w{},facing_w{};
  if (!torso_detail::RootFrame(world_from_model,right_w,up_w,facing_w,root)) return false;
  const float native_rel=RelativeYaw(facing_w,hr,hf);
  const float desired=std::clamp(desired_yaw,-kTorsoTwistLimitRad,kTorsoTwistLimitRad);
  const float delta=std::clamp(WrapAngleRad(desired-native_rel),
                              -kTorsoCorrectionLimitRad,kTorsoCorrectionLimitRad);
  Vec3 up_m{};float yaw=0;
  const float limited=native_rel+delta;
  const Vec3 desired_w=VAdd(VScale(hf,std::cos(limited)),VScale(hr,std::sin(limited)));
  if (!torso_detail::ModelYawAngle(root,facing_w,desired_w,up_m,yaw)) return false;
  std::array<ArmPoseMatrix,torso_detail::kPoseCapacity> scratch{};
  std::copy(poses.begin(),poses.end(),scratch.begin());
  const std::span<ArmPoseMatrix> work(scratch.data(),poses.size());
  const float pelvis=bones.pelvis>=0?std::clamp(yaw*kPelvisYawShare,-kPelvisYawCapRad,kPelvisYawCapRad):0;
  if (std::fabs(pelvis)>=1e-5f && !RotatePoseSubtree(work,parents,bones.pelvis,up_m,
      ArmPoseOrigin(work[bones.pelvis]),pelvis)) return false;
  const float weights[3]={.45f,.35f,.20f},caps[3]={.45f,.35f,.50f};
  const auto joints=DistributeJointAngles(yaw-pelvis,weights,caps);
  const int joint_bones[3]={bones.spine,bones.chest,bones.neck};
  for (int i=0;i<3;++i)
    if (std::fabs(joints[i])>=1e-5f && !RotatePoseSubtree(work,parents,joint_bones[i],up_m,
        ArmPoseOrigin(work[joint_bones[i]]),joints[i])) return false;
  const float pitch=std::clamp(pitch_share,-kLeanPitchCapRad,kLeanPitchCapRad);
  if (std::fabs(pitch)>=1e-5f) {
    Vec3 native_right_m{};
    if (!ArmPoseInversePoint(root,right_w,native_right_m)) return false;
    const float parity=VDot(VCross(arm_pose_detail::Column(root,0),
        arm_pose_detail::Column(root,1)),arm_pose_detail::Column(root,2))<0?-1.f:1.f;
    float cumulative=pelvis;
    for (int i=0;i<2;++i) {
      cumulative+=joints[i];
      const Vec3 axis=arm_pose_detail::AxisRotation(up_m,cumulative).Apply(VNorm(native_right_m));
      if (!RotatePoseSubtree(work,parents,joint_bones[i],axis,ArmPoseOrigin(work[joint_bones[i]]),
          -pitch*(i?.4f:.6f)*parity)) return false;
    }
  }
  Vec3 camera_m{};
  if (!ArmPoseInversePoint(root,cf,camera_m)) return false;
  const Vec3 head_f=VNorm(VScale(arm_pose_detail::Column(work[bones.head],2),-1));
  const Vec3 a=arm_pose_detail::Sub(head_f,VScale(up_m,VDot(head_f,up_m)));
  const Vec3 b=arm_pose_detail::Sub(camera_m,VScale(up_m,VDot(camera_m,up_m)));
  float snap=0;
  if (VLen(a)>=1e-4f && VLen(b)>=1e-4f) {
    const float residual=std::atan2(VDot(VCross(VNorm(a),VNorm(b)),up_m),VDot(VNorm(a),VNorm(b)));
    snap=std::clamp(joints[2]+residual,-kHeadSnapCapRad,kHeadSnapCapRad)-joints[2];
    if (std::fabs(snap)>=1e-4f && !RotatePoseSubtree(work,parents,bones.neck,up_m,
        ArmPoseOrigin(work[bones.neck]),snap)) return false;
  }
  std::copy(work.begin(),work.end(),poses.begin());
  result.valid=true;result.applied_delta=delta;
  result.pelvis_applied=pelvis;result.snap_applied=std::fabs(snap);return true;
}

// Exponential approach for torso state: 90 ms time constant, small
// deadband so resting micro-jitter never reaches the body, snap on the
// first sample and after long gaps. Angles wrap on the shortest arc.
class DampedAngle {
 public:
  float Approach(float target, std::uint64_t now_ms) {
    if (!std::isfinite(target)) return value_;
    if (!primed_) {
      primed_ = true;
      last_ms_ = now_ms;
      value_ = target;
      return value_;
    }
    if (now_ms <= last_ms_) return value_;
    const std::uint64_t dt = now_ms - last_ms_;
    last_ms_ = now_ms;
    if (dt > 250) {
      value_ = target;
      return value_;
    }
    const float delta = WrapAngleRad(target - value_);
    if (std::fabs(delta) < kTorsoDampDeadbandRad) return value_;
    const float alpha =
        1.0f - std::exp(-static_cast<float>(dt) / kTorsoDampTauMs);
    value_ = WrapAngleRad(value_ + delta * alpha);
    return value_;
  }
  void Reset() { primed_ = false; value_ = 0; last_ms_ = 0; }
  bool Primed() const { return primed_; }
  float Value() const { return value_; }

 private:
  bool primed_ = false;
  float value_ = 0;
  std::uint64_t last_ms_ = 0;
};

class DampedVec3 {
 public:
  Vec3 Approach(Vec3 target, std::uint64_t now_ms) {
    if (!FiniteVec(target)) return value_;
    if (!primed_) {
      primed_ = true;
      last_ms_ = now_ms;
      value_ = target;
      return value_;
    }
    if (now_ms <= last_ms_) return value_;
    const std::uint64_t dt = now_ms - last_ms_;
    last_ms_ = now_ms;
    if (dt > 250) {
      value_ = target;
      return value_;
    }
    const Vec3 delta = arm_pose_detail::Sub(target, value_);
    if (VLen(delta) < kTorsoDampDeadbandM) return value_;
    const float alpha =
        1.0f - std::exp(-static_cast<float>(dt) / kTorsoDampTauMs);
    value_ = VAdd(value_, VScale(delta, alpha));
    return value_;
  }
  void Reset() { primed_ = false; value_ = {}; last_ms_ = 0; }
  bool Primed() const { return primed_; }
  Vec3 Value() const { return value_; }

 private:
  bool primed_ = false;
  Vec3 value_{};
  std::uint64_t last_ms_ = 0;
};

}  // namespace dsvr
