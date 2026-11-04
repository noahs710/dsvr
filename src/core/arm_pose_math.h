#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>

#include "arm_ik_math.h"

namespace dsvr {

// Native model-space affine poses: row-major 3x4, column vectors, translation
// at 3/7/11. All points and link lengths use the model's existing units.
struct ArmPoseMatrix {
  float m[12];
};
static_assert(sizeof(ArmPoseMatrix) == 48);

struct ArmPoseBones {
  int upper = -1, forearm = -1, hand = -1, upper_twist = -1, weapon = -1,
      fore_twist = -1;
  // Optional direct parent of upper; used to share tracked swing with the shoulder.
  int clavicle = -1;
  // Aim the upper-arm basis so its authored hinge axis (column 2, the elbow
  // bend direction in captured native animation) follows the solved bend
  // plane instead of shortest-arc parallel transport. Opt-in: chains whose
  // bone basis convention differs (legs) keep the FromTo policy.
  bool hinge_aim = false;
};

struct ArmPoseResult {
  bool valid = false, reached = false;
  Vec3 elbow{}, wrist{};
  // Plane the two-bone solve lived in: `swing_axis` is its normal,
  // `hinge_side` the unit elbow offset direction. Both stay continuous along a
  // moving target, so a caller can decompose a shoulder swing into in-plane
  // swing and out-of-plane roll without deriving an axis from a shortest-arc
  // rotation, which has no defined axis once the swing nears a half turn.
  Vec3 swing_axis{}, hinge_side{};
};

// Optional rig-bound link lengths measured from the native skeleton pose.
// Invalid values are rejected rather than silently remeasured from animation.
struct ArmPoseLengths {
  float upper = 0, forearm = 0;
};

// Bend-plane rate budget for the production per-frame solve (degrees per
// solve). The chain core's pole/native handover is endpoint-correct but can
// cross a ~90 deg bend-plane disagreement in a dozen 90 Hz frames, rotating
// the UpperArm transport plane 13-69 deg per frame where the authored native
// bound is 12 (arm_ik_naturalness probe: cross-body f87-90 reaches 68.8 deg,
// agree(pole_perp, continuation) crossing zero while the pole cot-swings).
// Solving with this budget walks the bend toward its steady answer at a
// bounded rate about the chord; native animation itself turns the elbow plane
// at most 2.5 deg/frame (walk capture max), so 5 deg/frame leaves the live
// rig responsive while keeping every frame inside the authored bound.
inline constexpr float kArmBendMaxStepDeg = 4.f;

// Continuity filter for the head-anchored elbow pole. The production pole is
// recomputed from the wrist lift every frame (ArmIkPublishTargets) and swings
// ~55 deg as the hand crosses shoulder height; fed raw, SolveTwoBoneIk's bend
// plane swings with it and the elbow whips up to 0.26 m in one frame
// (arm_ik_naturalness cross-body/overhead). This is an exponential direction
// approach with a fixed time constant; state lives in the caller so rig
// rebinds and calibration changes can reset it explicitly. arm_ik.cpp and the
// naturalness fixture share this filter, so the fixture exercises the same
// smoothing the live rig applies.
struct ArmPoleSmoother {
  Vec3 value{};
  float tau_ms = 120.f;
  void Reset() { value = {}; }
  bool valid() const { return FiniteVec(value) && VLen(value) > .99f; }
  Vec3 Update(Vec3 desired, float dt_ms) {
    if (!FiniteVec(desired) || !(VLen(desired) > 1e-5f)) return {};
    const Vec3 want = VScale(desired, 1.f / VLen(desired));
    // A held input is a fixed point, including bit identity: tiny roundoff in
    // repeated blend/normalize steps must not spend a new bend-plane budget.
    if (want.x == value.x && want.y == value.y && want.z == value.z) return value;
    if (!valid() || !(tau_ms > 1e-3f) || !std::isfinite(dt_ms) || dt_ms < 0.f) {
      value = want; return value;
    }
    if (dt_ms == 0.f) return value;  // a duplicate callback has no time to spend
    // Near-opposite directions leave no arc to interpolate: step to the new
    // pole instead of blending through a noise-chosen perpendicular.
    if (VDot(value, want) < -.999f) { value = want; return value; }
    const float k = dt_ms >= tau_ms ? 1.f
                                    : 1.f - std::exp(-dt_ms / tau_ms);
    const Vec3 blended = VAdd(VScale(value, 1.f - k), VScale(want, k));
    const float length = VLen(blended);
    if (!(length > 1e-4f)) { value = want; return value; }
    value = VScale(blended, 1.f / length);
    return value;
  }
};

inline Vec3 ArmPoseOrigin(const ArmPoseMatrix& pose) {
  return {pose.m[3], pose.m[7], pose.m[11]};
}

inline Vec3 ArmPoseTransformPoint(const ArmPoseMatrix& pose, Vec3 point) {
  const float* m = pose.m;
  return {m[0] * point.x + m[1] * point.y + m[2] * point.z + m[3],
          m[4] * point.x + m[5] * point.y + m[6] * point.z + m[7],
          m[8] * point.x + m[9] * point.y + m[10] * point.z + m[11]};
}

namespace arm_pose_detail {

inline Vec3 Sub(Vec3 a, Vec3 b) { return VAdd(a, VScale(b, -1)); }
inline Vec3 Column(const ArmPoseMatrix& pose, int column) {
  return {pose.m[column], pose.m[4 + column], pose.m[8 + column]};
}

// Orthogonal scaled bases are supported, including reflected axes. Singular
// or sheared bases cannot safely use the inverse or retain a rigid stance.
inline bool ValidMatrix(const ArmPoseMatrix& pose) {
  for (float value : pose.m)
    if (!std::isfinite(value)) return false;
  Vec3 axes[3];
  for (int i = 0; i < 3; ++i) {
    Vec3 column = Column(pose, i);
    float squared = VDot(column, column);
    if (!std::isfinite(squared) || squared <= 1e-12f) return false;
    axes[i] = VScale(column, 1 / std::sqrt(squared));
  }
  return std::fabs(VDot(axes[0], axes[1])) < 1e-3f &&
         std::fabs(VDot(axes[0], axes[2])) < 1e-3f &&
         std::fabs(VDot(axes[1], axes[2])) < 1e-3f;
}

struct Rotation {
  float q[4] = {0, 0, 0, 1};
  Vec3 Apply(Vec3 v) const { return QuatRotate(v, q[0], q[1], q[2], q[3]); }
};

// Proper model-space rotation between two orthonormal bases of equal parity.
// The largest-diagonal branch keeps the 180-degree case well conditioned.
inline Rotation BasisRotation(const Vec3 from[3],const Vec3 to[3]) {
  float r[3][3]{};
  for(int axis=0;axis<3;++axis) {
    const float a[]={from[axis].x,from[axis].y,from[axis].z};
    const float b[]={to[axis].x,to[axis].y,to[axis].z};
    for(int row=0;row<3;++row)for(int col=0;col<3;++col)r[row][col]+=b[row]*a[col];
  }
  Rotation result;
  const float trace=r[0][0]+r[1][1]+r[2][2];
  if(trace>0) {
    const float s=2*std::sqrt(1+trace);
    result.q[3]=.25f*s;
    result.q[0]=(r[2][1]-r[1][2])/s;
    result.q[1]=(r[0][2]-r[2][0])/s;
    result.q[2]=(r[1][0]-r[0][1])/s;
  } else {
    int i=0;for(int n=1;n<3;++n)if(r[n][n]>r[i][i])i=n;
    const int j=(i+1)%3,k=(i+2)%3;
    const float s=2*std::sqrt(std::max(0.f,1+r[i][i]-r[j][j]-r[k][k]));
    if(s<1e-6f)return result;
    result.q[i]=.25f*s;
    result.q[j]=(r[i][j]+r[j][i])/s;
    result.q[k]=(r[i][k]+r[k][i])/s;
    result.q[3]=(r[k][j]-r[j][k])/s;
  }
  return result;
}

inline Rotation AxisRotation(Vec3 unit,float angle) {
  Rotation result;const float s=std::sin(angle*.5f);
  result.q[0]=unit.x*s;result.q[1]=unit.y*s;result.q[2]=unit.z*s;
  result.q[3]=std::cos(angle*.5f);return result;
}

inline Rotation FromTo(Vec3 from, Vec3 to) {
  Vec3 f = VNorm(from), t = VNorm(to);
  float dot = VDot(f, t);
  dot = dot < -1 ? -1 : (dot > 1 ? 1 : dot);
  Vec3 axis = VCross(f, t);
  float w = 1 + dot;
  float length = std::sqrt(VDot(axis, axis) + w * w);
  Rotation result;
  if (length > 1e-6f) {
    result.q[0] = axis.x / length;
    result.q[1] = axis.y / length;
    result.q[2] = axis.z / length;
    result.q[3] = w / length;
  } else {
    Vec3 other = std::fabs(f.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    axis = VNorm(VCross(f, other));
    result.q[0] = axis.x;
    result.q[1] = axis.y;
    result.q[2] = axis.z;
    result.q[3] = 0;
  }
  return result;
}

// Shortest-arc rotation, except when `from` and `to` are antiparallel and no
// arc exists at all. There the caller supplies the plane the joint bends in:
// a half turn about n and about -n are the same rotation, so bending in that
// plane is the unique answer, and it is the continuous limit of the shortest
// arc for a bend that stays in the plane. FromTo's arbitrary perpendicular
// instead flips sign between frames, spinning everything below the joint by
// 100+ degrees in one frame (arm_ik_naturalness cross-body/overhead, where the
// animated forearm passes opposite the solved forearm on a hard elbow fold).
inline Rotation FromToInPlane(Vec3 from, Vec3 to, Vec3 bend_plane_normal) {
  const Vec3 f = VNorm(from), t = VNorm(to);
  float dot = VDot(f, t);
  dot = dot < -1 ? -1 : (dot > 1 ? 1 : dot);
  if (dot > -0.999999f) return FromTo(from, to);
  const Vec3 n = VNorm(bend_plane_normal);
  Vec3 axis = Sub(n, VScale(f, VDot(n, f)));
  if (!(VLen(axis) > 1e-4f)) {
    const Vec3 other = std::fabs(f.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    axis = Sub(other, VScale(f, VDot(other, f)));
  }
  if (!(VLen(axis) > 1e-4f)) return AxisRotation(Vec3{0, 1, 0}, 0.0f);
  return AxisRotation(VNorm(axis), 3.14159265358979f);
}

template<class R>
inline ArmPoseMatrix Rotate(const ArmPoseMatrix& pose, const R& rotation,
                            Vec3 pivot) {
  ArmPoseMatrix result = pose;
  for (int i = 0; i < 3; ++i) {
    Vec3 column = rotation.Apply(Column(pose, i));
    result.m[i] = column.x;
    result.m[4 + i] = column.y;
    result.m[8 + i] = column.z;
  }
  Vec3 origin = VAdd(pivot, rotation.Apply(Sub(ArmPoseOrigin(pose), pivot)));
  result.m[3] = origin.x;
  result.m[7] = origin.y;
  result.m[11] = origin.z;
  return result;
}

// Called only after every parent and cycle has been checked.
inline bool InSubtree(int bone, int ancestor, std::span<const int> parents) {
  for (int current = bone; current != -1; current = parents[current])
    if (current == ancestor) return true;
  return false;
}

}  // namespace arm_pose_detail

// Inverse of a model-to-world affine matrix with orthogonal scaled columns.
// Failure leaves the caller's result unchanged.
inline bool ArmPoseInversePoint(const ArmPoseMatrix& pose, Vec3 point,
                                Vec3& result) {
  using namespace arm_pose_detail;
  if (!FiniteVec(point) || !ValidMatrix(pose)) return false;
  Vec3 delta = Sub(point, ArmPoseOrigin(pose));
  Vec3 axes[3] = {Column(pose, 0), Column(pose, 1), Column(pose, 2)};
  Vec3 local{VDot(delta, axes[0]) / VDot(axes[0], axes[0]),
             VDot(delta, axes[1]) / VDot(axes[1], axes[1]),
             VDot(delta, axes[2]) / VDot(axes[2], axes[2])};
  if (!FiniteVec(local)) return false;
  result = local;
  return true;
}


enum class NeutralHandBasisSource : unsigned char {
  NativeBind,
  MirroredNativeBind,
  TposeFallback
};

inline bool NeutralHandBasisDiagnosticReady(int hand_bone,
                                            bool source_reference_valid) {
  return hand_bone >= 0 && source_reference_valid;
}

inline bool NeutralHandFallbackBasis(unsigned side, Vec3 right, Vec3 up,
                                     Vec3 forward, ArmPoseMatrix& out) {
  if (side > 1 || !FiniteVec(right) || !FiniteVec(up) || !FiniteVec(forward) ||
      VLen(right) <= 1e-6f || VLen(up) <= 1e-6f || VLen(forward) <= 1e-6f)
    return false;
  right = VNorm(right);
  up = VNorm(up);
  forward = VNorm(forward);
  const float side_sign = side ? 1.0f : -1.0f;
  const Vec3 axes[3] = {VScale(right, side_sign), VScale(forward, side_sign),
                        VScale(up, -1.0f)};
  out = {};
  for (int axis = 0; axis < 3; ++axis) {
    out.m[axis] = axes[axis].x;
    out.m[4 + axis] = axes[axis].y;
    out.m[8 + axis] = axes[axis].z;
  }
  return arm_pose_detail::ValidMatrix(out);
}

inline bool ResolveNeutralHandBases(const ArmPoseMatrix native[2],
                                    const bool native_valid[2], Vec3 right,
                                    Vec3 up, Vec3 forward,
                                    ArmPoseMatrix resolved[2],
                                    NeutralHandBasisSource source[2]) {
  if (!native || !native_valid || !resolved || !source) return false;
  ArmPoseMatrix fallback[2]{};
  if (!NeutralHandFallbackBasis(0, right, up, forward, fallback[0]) ||
      !NeutralHandFallbackBasis(1, right, up, forward, fallback[1]))
    return false;
  const bool have_native[2] = {
      native_valid[0] && arm_pose_detail::ValidMatrix(native[0]),
      native_valid[1] && arm_pose_detail::ValidMatrix(native[1])};
  for (unsigned side = 0; side < 2; ++side) {
    resolved[side] = have_native[side] ? native[side] : fallback[side];
    source[side] = have_native[side] ? NeutralHandBasisSource::NativeBind
                                     : NeutralHandBasisSource::TposeFallback;
  }
  // ponytail: each side keeps its own bind. A missing bind falls back to
  // that side's own T-pose basis, never to the other hand's deviation.
  return arm_pose_detail::ValidMatrix(resolved[0]) &&
         arm_pose_detail::ValidMatrix(resolved[1]);
}

// Native hand orientation is a calibration reference, not a live animation.
// Cache a valid bind pair per calibration and rig revision so later animation
// frames cannot rotate the controller's neutral wrist.
class NeutralHandBasisCache {
 public:
  bool Resolve(const ArmPoseMatrix native[2], const bool native_valid[2],
               Vec3 right, Vec3 up, Vec3 forward,
               std::uint64_t calibration_revision,
               std::uint64_t rig_revision,
               ArmPoseMatrix resolved[2],
               NeutralHandBasisSource source[2]) {
    if (!native || !native_valid || !resolved || !source) return false;
    if (!valid_ || revision_ != calibration_revision || rig_revision_ != rig_revision) {
      if (revision_ != calibration_revision || rig_revision_ != rig_revision) valid_ = false;
      ArmPoseMatrix candidate[2]{};
      NeutralHandBasisSource candidate_source[2]{};
      if (!ResolveNeutralHandBases(native, native_valid, right, up, forward,
                                   candidate, candidate_source))
        return false;
      resolved[0] = candidate[0];
      resolved[1] = candidate[1];
      source[0] = candidate_source[0];
      source[1] = candidate_source[1];
      const bool have_native =
          (native_valid[0] && arm_pose_detail::ValidMatrix(native[0])) ||
          (native_valid[1] && arm_pose_detail::ValidMatrix(native[1]));
      if (have_native) {
        bases_[0] = candidate[0];
        bases_[1] = candidate[1];
        sources_[0] = candidate_source[0];
        sources_[1] = candidate_source[1];
        revision_ = calibration_revision;
        rig_revision_ = rig_revision;
        valid_ = true;
      }
      return true;
    }
    resolved[0] = bases_[0];
    resolved[1] = bases_[1];
    source[0] = sources_[0];
    source[1] = sources_[1];
    return true;
  }

 private:
  ArmPoseMatrix bases_[2]{};
  NeutralHandBasisSource sources_[2]{};
  std::uint64_t revision_ = 0;
  std::uint64_t rig_revision_ = 0;
  bool valid_ = false;
};

// Follow controller rotation relative to a captured native held stance. The
// controller basis may contain the XR/native handedness reflection; using the
// complete calibrated bases cancels it without guessing wrist or weapon axes.
inline bool ArmPoseGripTarget(const ArmPoseMatrix& controller,
                              const ArmPoseMatrix& reference_controller,
                              const ArmPoseMatrix& reference_hand,
                              ArmPoseMatrix& target) {
  using namespace arm_pose_detail;
  auto unit = [](const ArmPoseMatrix& pose, ArmPoseMatrix& out) {
    if (!ValidMatrix(pose)) return false;
    out={};
    for (int axis=0;axis<3;++axis) {
      const auto v=VNorm(Column(pose,axis));
      out.m[axis]=v.x;out.m[4+axis]=v.y;out.m[8+axis]=v.z;
    }
    return true;
  };
  ArmPoseMatrix current{}, reference{}, hand{}, desired{};
  if (!unit(controller,current) || !unit(reference_controller,reference) ||
      !unit(reference_hand,hand)) return false;
  for (int axis=0;axis<3;++axis) {
    Vec3 local{};
    if (!ArmPoseInversePoint(reference,Column(hand,axis),local)) return false;
    const auto v=ArmPoseTransformPoint(current,local);
    desired.m[axis]=v.x;desired.m[4+axis]=v.y;desired.m[8+axis]=v.z;
  }
  if (!ValidMatrix(desired)) return false;
  target=desired;return true;
}

// Wrist origin that places the native weapon/grip point on the controller.
// The hand-to-grip offset is a rigid point offset: it rotates with the
// desired hand basis and keeps the measured live length (about 0.061 m).
// Failure leaves the caller's result unchanged.
inline bool ArmPoseWristForGrip(Vec3 grip_target,
                                const ArmPoseMatrix& desired_hand,
                                const ArmPoseMatrix& reference_hand,
                                const ArmPoseMatrix& reference_weapon,
                                Vec3& wrist_target) {
  using namespace arm_pose_detail;
  if (!FiniteVec(grip_target) || !ValidMatrix(desired_hand) ||
      !ValidMatrix(reference_hand) || !ValidMatrix(reference_weapon))
    return false;
  Vec3 from[3], to[3];
  for (int axis = 0; axis < 3; ++axis) {
    from[axis] = VNorm(Column(reference_hand, axis));
    to[axis] = VNorm(Column(desired_hand, axis));
  }
  const float parity = VDot(VCross(from[0], from[1]), from[2]) *
                       VDot(VCross(to[0], to[1]), to[2]);
  if (parity < .999f) return false;
  const Vec3 offset = Sub(ArmPoseOrigin(reference_weapon),
                          ArmPoseOrigin(reference_hand));
  Vec3 rotated{};
  for (int axis = 0; axis < 3; ++axis)
    rotated = VAdd(rotated, VScale(to[axis], VDot(from[axis], offset)));
  const Vec3 result = Sub(grip_target, rotated);
  if (!FiniteVec(result)) return false;
  wrist_target = result;
  return true;
}

// With a forearm-twist sibling supplied, axial pronation is one rigid
// rotation of the whole below-elbow subtree (Forearm, ForeTwist, Hand and
// their descendants) about the elbow->wrist axis through the elbow. The
// wrist origin lies on the axis, so it does not move; Forearm and ForeTwist
// receive the identical rotation and their authored relative pose survives
// exactly (captured native animation keeps that axial offset within ~6 deg
// and half-angle splits shear the forearm skin; live report: the forearm
// mesh twisted/pinched). The hand subtree then takes only the twist-free
// swing remainder about the wrist, so hand-vs-Forearm keeps pure wrist
// flexion while the grip orientation is met exactly. Native
// finger/equipment transforms and every bone scale survive. Invalid
// hierarchy/basis/parity leaves the whole array unchanged.
inline bool OrientModelHandPose(std::span<ArmPoseMatrix> poses,
                                std::span<const int> parents, int hand,
                                const ArmPoseMatrix& target, int fore_twist = -1) {
  using namespace arm_pose_detail;
  const auto count=poses.size();
  if (!count || count>static_cast<size_t>((std::numeric_limits<int>::max)()) ||
      count!=parents.size() || hand<0 || static_cast<size_t>(hand)>=count ||
      parents[hand]==-1 || !ValidMatrix(target)) return false;
  for (size_t i=0;i<count;++i) {
    for(float value:poses[i].m)if(!std::isfinite(value))return false;
    if (parents[i]<-1 || (parents[i]>=0 && static_cast<size_t>(parents[i])>=count)) return false;
  }
  for (size_t i=0;i<count;++i) {
    int current=static_cast<int>(i);size_t steps=0;
    while (current!=-1 && steps++<count) current=parents[current];
    if (current!=-1) return false;
  }
  for(size_t i=0;i<count;++i)
    if(InSubtree(static_cast<int>(i),hand,parents) && !ValidMatrix(poses[i]))return false;
  struct BasisDelta {
    Vec3 from[3],to[3];
    Vec3 Apply(Vec3 v) const {
      return VAdd(VAdd(VScale(to[0],VDot(from[0],v)),
                       VScale(to[1],VDot(from[1],v))),VScale(to[2],VDot(from[2],v)));
    }
  } delta{};
  for (int axis=0;axis<3;++axis) {
    delta.from[axis]=VNorm(Column(poses[hand],axis));
    delta.to[axis]=VNorm(Column(target,axis));
  }
  const float parity=VDot(VCross(delta.from[0],delta.from[1]),delta.from[2]) *
                     VDot(VCross(delta.to[0],delta.to[1]),delta.to[2]);
  if (parity<.999f) return false;
  const auto pivot=ArmPoseOrigin(poses[hand]);
  const int forearm=parents[hand];
  Rotation pronation{},swing{};Vec3 elbow{};
  if(fore_twist!=-1) {
    if(fore_twist<0 || static_cast<size_t>(fore_twist)>=count ||
        fore_twist==hand || fore_twist==forearm || parents[forearm]<0 ||
        parents[fore_twist]!=parents[forearm])return false;
    elbow=ArmPoseOrigin(poses[forearm]);
    const Vec3 axis=VNorm(Sub(pivot,elbow));
    if(VLen(axis)<.99f)return false;
    const auto rotation=BasisRotation(delta.from,delta.to);
    // Axial (pronation) part about the elbow->wrist axis, taken from the
    // canonical rotation vector instead of 2*atan2(v.axis, w). At a half-turn
    // grip delta (w = 0, which happens when the tracked hand swings to the far
    // side of the elbow) that atan2 is undefined and its sign is floating-point
    // noise, so the forearm subtree flipped by pi between consecutive frames.
    // theta*(k.axis) is identical wherever the split is well defined, is the
    // shortest-rotation branch, and stays continuous through the half turn; the
    // swing remainder below still closes the hand basis exactly.
    Vec3 rot_vec{rotation.q[0], rotation.q[1], rotation.q[2]};
    float real = rotation.q[3];
    if (real < 0.0f) { rot_vec = VScale(rot_vec, -1.0f); real = -real; }
    const float rot_len = VLen(rot_vec);
    float angle = 0;
    if (rot_len > 1e-6f)
      angle = 2.0f * std::atan2(rot_len, real) *
              VDot(VScale(rot_vec, 1.0f / rot_len), axis);
    pronation=AxisRotation(axis,angle);
    // Swing remainder for the hand after the rigid pronation: rotating the
    // post-pronation hand basis onto the target is exactly swing = D*P^-1,
    // the unique rotation mapping one orthonormal basis to the other.
    Vec3 post[3];
    for (int k = 0; k < 3; ++k) post[k] = pronation.Apply(delta.from[k]);
    swing=BasisRotation(post,delta.to);
  }
  const auto affected=[&](int i) {
    return InSubtree(i,hand,parents) || (fore_twist!=-1 &&
        (InSubtree(i,forearm,parents) || InSubtree(i,fore_twist,parents)));
  };
  // The twist skin is a skin-only bone: captured native animation never
  // translates it. Rotating it about the elbow drags its authored origin
  // sideways whenever it is not exactly on the elbow->wrist axis (a few mm in
  // the live rig, 0.12 m in the off-axis fixture) and pinches the forearm mesh.
  // Give that branch the identical rotation about the twist bone's own origin;
  // its relative basis against the forearm is then preserved exactly, which is
  // what the ForeTwist<->Forearm axial bound measures.
  const Vec3 twist_pivot=fore_twist!=-1?ArmPoseOrigin(poses[fore_twist]):Vec3{};
  const auto candidate=[&](int i) {
    if(InSubtree(i,hand,parents))
      return fore_twist!=-1
          ? Rotate(Rotate(poses[i],pronation,elbow),swing,pivot)
          : Rotate(poses[i],delta,pivot);
    if(fore_twist!=-1&&InSubtree(i,fore_twist,parents))
      return Rotate(poses[i],pronation,twist_pivot);
    return Rotate(poses[i],pronation,elbow);
  };
  for(size_t i=0;i<count;++i)
    if(affected(static_cast<int>(i)) && (!ValidMatrix(poses[i]) ||
        !ValidMatrix(candidate(static_cast<int>(i)))))return false;
  for(size_t i=0;i<count;++i)
    if(affected(static_cast<int>(i)))poses[i]=candidate(static_cast<int>(i));
  return true;
}

// Rotate a hand basis around a model-space axis (Rodrigues via the shared
// quaternion type). Used for the mirrored grip-roll correction: rolling
// around the wrist->grip axis turns the palm without moving the grip point.
inline bool ArmPoseRollBasis(const ArmPoseMatrix& basis, Vec3 axis,
                             float angle_rad, ArmPoseMatrix& rolled) {
  using namespace arm_pose_detail;
  if (!ValidMatrix(basis) || !FiniteVec(axis) || !std::isfinite(angle_rad))
    return false;
  Vec3 unit = VNorm(axis);
  if (VLen(unit) < .99f) return false;
  const float half = angle_rad * .5f, s = std::sin(half);
  Rotation rotation;
  rotation.q[0] = unit.x * s;
  rotation.q[1] = unit.y * s;
  rotation.q[2] = unit.z * s;
  rotation.q[3] = std::cos(half);
  ArmPoseMatrix result = basis;
  for (int column = 0; column < 3; ++column) {
    Vec3 rotated = rotation.Apply(Column(basis, column));
    result.m[column] = rotated.x;
    result.m[4 + column] = rotated.y;
    result.m[8 + column] = rotated.z;
  }
  if (!ValidMatrix(result)) return false;
  rolled = result;
  return true;
}

// Solve an existing upper->forearm->hand chain, retaining its measured link
// lengths and bend side. Two shortest-arc model-space rotations preserve each
// bone's scale and twist. Their rigid deltas carry fingers/weapon descendants
// with the hand; an optional upper-twist sibling follows only the upper arm.
// No controller grip orientation or bone-axis mapping is assumed.
//
// Parents use -1 for roots and may appear in any order. Malformed hierarchies,
// non-finite poses, invalid bases or degenerate links leave ALL poses unchanged.
inline ArmPoseResult SolveModelArmPose(std::span<ArmPoseMatrix> poses,
                                       std::span<const int> parents,
                                       const ArmPoseBones& bones,
                                       Vec3 wrist_target,
                                       const Vec3* preferred_pole = nullptr,
                                       const ArmPoseLengths* calibrated_lengths = nullptr,
                                       float max_bend_step_deg = 0.f,
                                       ArmBendState* bend_state = nullptr,
                                       const Vec3* forced_bend = nullptr) {
  using namespace arm_pose_detail;
  ArmPoseResult result;
  const size_t count = poses.size();
  if (count == 0 || count != parents.size() ||
      count > static_cast<size_t>((std::numeric_limits<int>::max)()) ||
      !FiniteVec(wrist_target)) return result;
  auto valid_index = [count](int index) {
    return index >= 0 && static_cast<size_t>(index) < count;
  };
  if (!valid_index(bones.upper) || !valid_index(bones.forearm) ||
      !valid_index(bones.hand) || bones.upper_twist < -1 ||
      (bones.upper_twist != -1 && !valid_index(bones.upper_twist)) ||
      bones.fore_twist < -1 ||
      (bones.fore_twist != -1 && !valid_index(bones.fore_twist))) return result;
  for (size_t i = 0; i < count; ++i) {
    for(float value:poses[i].m)if(!std::isfinite(value))return result;
    if (parents[i] != -1 && !valid_index(parents[i])) return result;
  }
  for (size_t i = 0; i < count; ++i) {
    int current = static_cast<int>(i);
    size_t steps = 0;
    while (current != -1 && steps++ < count) current = parents[current];
    if (current != -1) return result;
  }
  if (parents[bones.upper] == -1 || parents[bones.forearm] != bones.upper ||
      parents[bones.hand] != bones.forearm) return result;
  if (bones.upper_twist != -1 &&
      (bones.upper_twist == bones.upper ||
       parents[bones.upper_twist] != parents[bones.upper])) return result;
  // The FLVER forearm-twist bone is parented to the upper arm but skins the
  // forearm: without the forearm delta its verts shear against the solved
  // forearm (live report: forearm bones looked broken and bent).
  if (bones.fore_twist != -1 &&
      (bones.fore_twist == bones.upper || bones.fore_twist == bones.forearm ||
       bones.fore_twist == bones.hand ||
       bones.fore_twist == bones.upper_twist ||
       parents[bones.fore_twist] != bones.upper)) return result;

  // Native palette capacity includes unused zero-basis slots. Validate only
  // the branches this operation transforms; preserve unrelated slots exactly.
  auto affected=[&](int i) {
    return InSubtree(i,bones.upper,parents) ||
        (bones.upper_twist!=-1 && InSubtree(i,bones.upper_twist,parents));
  };
  for(size_t i=0;i<count;++i)
    if(affected(static_cast<int>(i)) && !ValidMatrix(poses[i]))return result;
  Vec3 shoulder = ArmPoseOrigin(poses[bones.upper]);
  Vec3 elbow = ArmPoseOrigin(poses[bones.forearm]);
  Vec3 wrist = ArmPoseOrigin(poses[bones.hand]);
  Vec3 upper = Sub(elbow, shoulder), forearm = Sub(wrist, elbow);
  ArmRig rig;
  rig.upper_length_m = calibrated_lengths ? calibrated_lengths->upper : VLen(upper);
  rig.forearm_length_m = calibrated_lengths ? calibrated_lengths->forearm : VLen(forearm);
  if (!std::isfinite(rig.upper_length_m) || !std::isfinite(rig.forearm_length_m) ||
      !(rig.upper_length_m > 1e-4f) || !(rig.forearm_length_m > 1e-4f)) return result;
  // Rigid transport cannot resize a mesh. Accept rig-bound lengths only while
  // they still describe this native chain; otherwise the reported solved wrist
  // would differ from the palette we actually write. Do not fake model scaling
  // by feeding longer links into a rotation-only transport.
  if (calibrated_lengths &&
      (std::fabs(rig.upper_length_m-VLen(upper))>std::max(1e-5f,VLen(upper)*1e-4f) ||
       std::fabs(rig.forearm_length_m-VLen(forearm))>std::max(1e-5f,VLen(forearm)*1e-4f)))
    return result;
  // Anatomical fold limit. The human elbow flexes to at most about 145 deg,
  // an interior joint angle of ~35-40 deg; past that the forearm drives into
  // the upper arm. A target inside that chord (for native 0.28/0.30 links,
  // 0.199 m from the shoulder) demands the wrist pass THROUGH the shoulder
  // joint itself: arm_ik_naturalness cross-body measured 0.05 m chords at
  // ~176 deg interior, where the solved elbow must traverse a half circle in
  // a few frames and per-frame continuity is unsatisfiable for any solver
  // (16-69 deg/frame basis deltas). Saturate the target to the fold boundary
  // radially from the shoulder: bounded, anatomical, and the direction to
  // the target is preserved, so onset has no kink.
  bool fold_clamped = false;
  {
    constexpr float kFoldCos = .7660444431f;  // cos(40 deg)
    const float l0 = rig.upper_length_m, l1 = rig.forearm_length_m;
    float fold_min = std::sqrt(std::max(0.f, l0 * l0 + l1 * l1 - 2.f * l0 * l1 * kFoldCos));
    const float degenerate = std::fabs(l0 - l1);
    if (fold_min < degenerate) fold_min = degenerate;
    const Vec3 rel = Sub(wrist_target, shoulder);
    const float dist = VLen(rel);
    if (dist < fold_min) {
      Vec3 radial = dist > 1e-6f ? VScale(rel, 1.f / dist) : VNorm(upper);
      if (!(VLen(radial) > .99f)) radial = Vec3{0, 1, 0};
      wrist_target = VAdd(shoulder, VScale(radial, fold_min));
      fold_clamped = true;
    }
  }
  Vec3 whole = Sub(wrist, shoulder);
  float whole_squared = VDot(whole, whole);
  Vec3 pole = upper;
  if (whole_squared > 1e-12f && std::isfinite(whole_squared))
    pole = Sub(upper, VScale(whole, VDot(upper, whole) / whole_squared));
  if (VLen(pole) < 1e-5f) {
    Vec3 direction = VNorm(upper);
    int axis = 0;
    for (int i = 1; i < 3; ++i)
      if (std::fabs(VDot(VNorm(Column(poses[bones.upper], i)), direction)) <
          std::fabs(VDot(VNorm(Column(poses[bones.upper], axis)), direction)))
        axis = i;
    pole = Column(poses[bones.upper], axis);
  }
  if (preferred_pole) {
    if (!FiniteVec(*preferred_pole) || VLen(*preferred_pole)<1e-5f) return result;
    pole=*preferred_pole;
  }
  // Production reach solver: FABRIK chain (2026-10-22 plan decision),
  // initialized from the live palette chain. With the pole well conditioned
  // about the solved chord the three-joint result equals SolveTwoBoneIk
  // exactly (characterized in test_arm_ik_math.cpp), which is why every
  // consumer below is unchanged; in degraded conditioning the chain core
  // blends the bend toward the native pose instead of whipping the elbow.
  const Vec3 chain_joints[3] = {shoulder, elbow, wrist};
  const float chain_links[2] = {rig.upper_length_m, rig.forearm_length_m};
  IkSolution solution = IkSolutionFromChain(SolveChainIkFabrik(
      chain_joints, chain_links, 3, wrist_target, &pole, 8,
      bend_state && bend_state->valid ? bend_state : nullptr,
      max_bend_step_deg, forced_bend));
  if (!solution.valid || VLen(solution.upper_dir) < 0.99f ||
      VLen(solution.forearm_dir) < 0.99f) return result;
  Rotation upper_delta = FromToInPlane(upper, Sub(solution.elbow, shoulder),
                                       solution.plane_normal);
  if (bones.hinge_aim) {
    // Captured native animation holds |dot(UpperArm column 2, elbow bend
    // direction)| = 1.000 across every idle and walk frame (64/64). Shortest-
    // arc transport rolls the basis freely, which reads as the upper arm
    // "not rotating naturally" and shears the torso skin at the armpit. Aim
    // the full basis instead: column 0 along the solved upper direction,
    // column 2 along the solved bend direction, column 1 closing the frame.
    const Vec3 target_upper = Sub(solution.elbow, shoulder);
    const Vec3 upper_dir = VNorm(target_upper);
    // Take the hinge from the solve plane, not from the solved forearm. The
    // forearm's component perpendicular to the upper arm collapses exactly when
    // the arm straightens, so deriving the hinge from it lets the aimed basis
    // flip by over 100 degrees in one frame on a reach that passes near full
    // extension (arm_ik_naturalness cross-body/overhead). solution.bend_dir is
    // the pole-driven elbow offset and stays unit length through extension.
    Vec3 bend = Sub(solution.bend_dir,
                    VScale(upper_dir, VDot(solution.bend_dir, upper_dir)));
    if (VLen(bend) < 1e-4f) {
      const Vec3 solved_forearm = Sub(solution.wrist, solution.elbow);
      bend = Sub(solved_forearm,
                 VScale(upper_dir, VDot(solved_forearm, upper_dir)));
    }
    if (VLen(bend) > 1e-4f) {
      const Vec3 hinge = VNorm(bend);
      const Vec3 aim[3] = {upper_dir, VCross(hinge, upper_dir), hinge};
      Vec3 current[3];
      for (int axis = 0; axis < 3; ++axis)
        current[axis] = VNorm(Column(poses[bones.upper], axis));
      upper_delta = BasisRotation(current, aim);
    }
  }
  Rotation forearm_delta = FromToInPlane(upper_delta.Apply(forearm),
                                         Sub(solution.wrist, solution.elbow),
                                         solution.plane_normal);
  auto candidate = [&](int index) {
    ArmPoseMatrix pose = poses[index];
    bool upper_branch = InSubtree(index, bones.upper, parents);
    bool twist_branch = bones.upper_twist != -1 &&
                        InSubtree(index, bones.upper_twist, parents);
    if (upper_branch || twist_branch) pose = Rotate(pose, upper_delta, shoulder);
    if (InSubtree(index, bones.forearm, parents) ||
        (bones.fore_twist != -1 &&
         InSubtree(index, bones.fore_twist, parents)))
      pose = Rotate(pose, forearm_delta, solution.elbow);
    return pose;
  };
  // Validate a complete pass before committing. Each candidate depends only
  // on its own original matrix and the fixed deltas, so no scratch allocation
  // or hierarchy ordering is needed and failure cannot expose a partial arm.
  for (size_t i = 0; i < count; ++i)
    if (affected(static_cast<int>(i)) && !ValidMatrix(candidate(static_cast<int>(i)))) return result;
  for (size_t i = 0; i < count; ++i) {
    int index = static_cast<int>(i);
    if (InSubtree(index, bones.upper, parents) ||
        (bones.upper_twist != -1 && InSubtree(index, bones.upper_twist, parents)))
      poses[i] = candidate(index);
  }
  result.valid = true;
  result.reached = solution.reached && !fold_clamped;
  result.elbow = solution.elbow;
  result.wrist = solution.wrist;
  result.swing_axis = solution.plane_normal;
  result.hinge_side = solution.bend_dir;
  // The committed pose achieved this bend plane; it becomes the rate-budget
  // reference for the next solve of this chain, fingerprinted with the exact
  // inputs so a re-processed identical solve reuses it bit-exactly.
  if (bend_state) {
    ArmBendState next;
    next.bend = solution.bend_dir;
    next.joints[0] = chain_joints[0];
    next.joints[1] = chain_joints[1];
    next.joints[2] = chain_joints[2];
    next.lengths[0] = chain_links[0];
    next.lengths[1] = chain_links[1];
    next.target = wrist_target;
    next.pole = VNorm(pole);
    next.valid = true;
    *bend_state = next;
  }
  return result;
}

// Shares a bounded part of upper-arm swing with the clavicle, then resolves
// the same wrist target from the shifted shoulder. Caller scratch keeps this
// per-frame adjustment allocation-free and transactional.
inline ArmPoseResult SolveModelArmPoseWithClavicle(
    std::span<ArmPoseMatrix> poses, std::span<const int> parents,
    const ArmPoseBones& bones, Vec3 wrist_target,
    std::span<ArmPoseMatrix> scratch, const Vec3* preferred_pole = nullptr,
    const ArmPoseLengths* calibrated_lengths = nullptr,
    float max_bend_step_deg = 0.f, ArmBendState* bend_state = nullptr) {
  using namespace arm_pose_detail;
  if (bones.clavicle == -1)
    return SolveModelArmPose(poses, parents, bones, wrist_target,
                             preferred_pole, calibrated_lengths,
                             max_bend_step_deg, bend_state);

  ArmPoseResult refused;
  const size_t count = poses.size();
  // The probe below is the frame's canonical solve: it sees the palette the
  // animation just produced, so it is what the bend rate reference is keyed
  // to. The re-solve from the clavicle-shifted shoulder is then pinned to the
  // plane the probe chose (see frame_bend below).
  if (bones.clavicle < 0 || static_cast<size_t>(bones.clavicle) >= count ||
      scratch.size() < count || scratch.data() == poses.data() ||
      bones.upper < 0 || static_cast<size_t>(bones.upper) >= count ||
      parents.size() != count || parents[bones.upper] != bones.clavicle)
    return refused;

  ArmPoseBones chain = bones;
  chain.clavicle = -1;
  ArmBendState next_bend = bend_state ? *bend_state : ArmBendState{};
  std::copy(poses.begin(), poses.end(), scratch.begin());
  const auto probe = SolveModelArmPose(scratch.first(count), parents, chain,
                                       wrist_target, preferred_pole,
                                       calibrated_lengths, max_bend_step_deg,
                                        bend_state ? &next_bend : nullptr);
  if (!probe.valid) return refused;

  const Vec3 shoulder = ArmPoseOrigin(poses[bones.upper]);
  const Vec3 upper = Sub(ArmPoseOrigin(poses[bones.forearm]), shoulder);
  const Vec3 target_upper = Sub(probe.elbow, shoulder);
  // Share only the part of the shoulder swing that lies in the plane the arm
  // solve lives in, as a signed angle about that plane's normal. The axis of
  // the shortest-arc rotation from the current upper arm to the solved one is
  // arbitrary once that swing nears a half turn, and reaching overhead from a
  // hanging idle passes exactly through it: deriving the share from that axis
  // snapped the whole subtree about a noise-determined axis (per-frame basis
  // deltas of 12-124 deg in arm_ik_naturalness, cross-body/overhead).
  const float upper_length = VLen(upper), target_length = VLen(target_upper);
  if (!(upper_length > 1e-4f) || !(target_length > 1e-4f)) return refused;
  const Vec3 upper_dir = VScale(upper, 1.0f / upper_length);
  const Vec3 target_dir = VScale(target_upper, 1.0f / target_length);
  const Vec3 axis = probe.swing_axis;
  if (!(VLen(axis) > .99f)) return refused;
  const Vec3 from_in = Sub(upper_dir, VScale(axis, VDot(upper_dir, axis)));
  const Vec3 to_in = Sub(target_dir, VScale(axis, VDot(target_dir, axis)));
  const float from_len = VLen(from_in), to_len = VLen(to_in);
  constexpr float kShoulderSwingShare = .25f;
  constexpr float kShoulderSwingLimitRad = .35f;
  float shoulder_angle = 0.0f;
  if (from_len > 1e-4f && to_len > 1e-4f) {
    const Vec3 fu = VScale(from_in, 1.0f / from_len);
    const Vec3 tu = VScale(to_in, 1.0f / to_len);
    const float swing = std::atan2(VDot(VCross(fu, tu), axis), VDot(fu, tu));
    if (!std::isfinite(swing)) return refused;
    // A fraction of a rotation is not defined at a half turn: +pi and -pi are
    // the same swing but their quarters point opposite ways. So the share is
    // proportional to the swing for ordinary motion and goes to zero at the
    // half turn instead of jumping sign. Overhead reaches from a hanging idle
    // pass exactly through that ambiguity, and a sign jump there moves the whole
    // clavicle subtree by 0.7 rad in one frame.
    // The in-plane projections also vanish near the half turn, which makes the
    // swing axis noise: blend the share off continuously as either projection
    // shrinks instead of snapping a full-strength angle to zero at a hard
    // gate (that snap moved the shoulder by its full share in one frame and
    // whipped the solved elbow with it).
    const float fade = std::clamp((std::min(from_len, to_len) - .05f) / .10f,
                                  0.0f, 1.0f);
    shoulder_angle = std::clamp(kShoulderSwingShare * std::sin(swing) * fade,
                                -kShoulderSwingLimitRad, kShoulderSwingLimitRad);
  }
  if (!std::isfinite(shoulder_angle)) return refused;
  const Vec3 pivot = ArmPoseOrigin(poses[bones.clavicle]);
  if (std::fabs(shoulder_angle) <= 1e-5f) {
    std::copy(scratch.begin(), scratch.begin() + count, poses.begin());
    if (bend_state) *bend_state = next_bend;
    return probe;
  }

  // Moving the shoulder must not push the wrist target out of the arm's reach
  // from there: a reach-clamped solve lands the wrist short of the tracked grip
  // and the whole arm stutters. For a = shoulder - pivot, b = target - pivot and
  // a rotation about `axis` by phi, |b - R(phi)a|^2 = |b|^2 + |a|^2 - 2*Z -
  // 2*(A cos phi + B sin phi) with A = a.b - Z, B = b.(axis x a),
  // Z = (a.axis)(b.axis). Requiring that to stay within reach gives
  // M cos(phi - psi) >= K/2, the arc of shares that still reach; the wanted
  // share is clipped into it. If no share reaches, take the one pointing
  // furthest at the target, still inside the anatomical cap.
  const float l1 = calibrated_lengths ? calibrated_lengths->upper : upper_length;
  const float l2 = calibrated_lengths
                       ? calibrated_lengths->forearm
                       : VLen(Sub(ArmPoseOrigin(poses[bones.hand]),
                                  ArmPoseOrigin(poses[bones.forearm])));
  if (!(l1 > 1e-4f) || !(l2 > 1e-4f)) return refused;
  const float reach = (l1 + l2) * (1.0f - 1e-6f);
  const Vec3 a = Sub(shoulder, pivot), b = Sub(wrist_target, pivot);
  const float az = VDot(a, axis), bz = VDot(b, axis);
  const float A = VDot(a, b) - az * bz, B = VDot(b, VCross(axis, a));
  const float M = std::sqrt(A * A + B * B);
  if (M > 1e-9f) {
    const float K = VDot(b, b) + VDot(a, a) - 2.0f * az * bz - reach * reach;
    const float psi = std::atan2(B, A), bound = K / (2.0f * M);
    if (!std::isfinite(bound)) return refused;
    if (bound > 1.0f) {
      shoulder_angle = std::clamp(psi, -kShoulderSwingLimitRad,
                                  kShoulderSwingLimitRad);
    } else if (bound > -1.0f) {
      const float half = std::acos(bound);
      float delta = shoulder_angle - psi;
      while (delta > 3.14159265f) delta -= 6.283185307f;
      while (delta < -3.14159265f) delta += 6.283185307f;
      if (std::fabs(delta) > half)
        shoulder_angle = std::clamp(psi + (delta > 0.0f ? half : -half),
                                    -kShoulderSwingLimitRad,
                                    kShoulderSwingLimitRad);
    }
  }
  if (!std::isfinite(shoulder_angle)) return refused;
  if (std::fabs(shoulder_angle) <= 1e-5f) {
    std::copy(scratch.begin(), scratch.begin() + count, poses.begin());
    if (bend_state) *bend_state = next_bend;
    return probe;
  }

  std::copy(poses.begin(), poses.end(), scratch.begin());
  const Rotation shoulder_delta = AxisRotation(axis, shoulder_angle);
  for (size_t i = 0; i < count; ++i) {
    if (!InSubtree(static_cast<int>(i), bones.clavicle, parents)) continue;
    if (!ValidMatrix(scratch[i])) return refused;
    const auto candidate = Rotate(scratch[i], shoulder_delta, pivot);
    if (!ValidMatrix(candidate)) return refused;
    scratch[i] = candidate;
  }

  // The probe spent this frame's rate budget and its committed reference is
  // keyed to the palette this frame was solved from, so replaying an unchanged
  // frame reuses it bit-exactly whichever side of a capture-millisecond
  // boundary the tick fell on. The final solve re-resolves the same wrist
  // target from the shifted shoulder in the plane that solve already chose:
  // one frame decides its bend plane once, and the whole clavicle subtree
  // keeps it (per-frame basis deltas in arm_ik_naturalness otherwise stepped
  // two budgets per frame and the reference drifted between replays).
  const Vec3* frame_bend =
      bend_state && max_bend_step_deg > 0.f ? &probe.hinge_side : nullptr;
  const auto result = SolveModelArmPose(
      scratch.first(count), parents, chain, wrist_target, preferred_pole,
      calibrated_lengths, frame_bend ? 0.f : max_bend_step_deg,
      frame_bend || !bend_state ? nullptr : &next_bend, frame_bend);
  if (!result.valid) return refused;
  std::copy(scratch.begin(), scratch.begin() + count, poses.begin());
  if (bend_state) *bend_state = next_bend;
  return result;
}

}  // namespace dsvr
