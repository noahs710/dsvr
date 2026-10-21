#pragma once

#include <algorithm>
#include <cmath>
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
};

struct ArmPoseResult {
  bool valid = false, reached = false;
  Vec3 elbow{}, wrist{};
};

// Optional rig-bound link lengths measured from the native skeleton pose.
// Invalid values are rejected rather than silently remeasured from animation.
struct ArmPoseLengths {
  float upper = 0, forearm = 0;
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

// With a forearm-twist sibling supplied, distribute axial pronation through
// the forearm and its twist skin. Pure wrist flexion remains at the wrist.
// Native finger/equipment transforms and every bone scale survive.
// Invalid hierarchy/basis/parity leaves the whole array unchanged.
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
  Rotation pronation{},twist_skin{};Vec3 elbow{};
  if(fore_twist!=-1) {
    if(fore_twist<0 || static_cast<size_t>(fore_twist)>=count ||
        fore_twist==hand || fore_twist==forearm || parents[forearm]<0 ||
        parents[fore_twist]!=parents[forearm])return false;
    elbow=ArmPoseOrigin(poses[forearm]);
    const Vec3 axis=VNorm(Sub(pivot,elbow));
    if(VLen(axis)<.99f)return false;
    const auto rotation=BasisRotation(delta.from,delta.to);
    const float projected=rotation.q[0]*axis.x+rotation.q[1]*axis.y+rotation.q[2]*axis.z;
    // Swing-twist decomposition about the current elbow->wrist axis. A pure
    // perpendicular half-turn has no defined axial component: keep it neutral.
    float angle=0;
    if(projected*projected+rotation.q[3]*rotation.q[3]>1e-12f)
      angle=std::remainder(2*std::atan2(projected,rotation.q[3]),6.283185307f);
    pronation=AxisRotation(axis,angle);
    twist_skin=AxisRotation(axis,angle*.5f);
  }
  const auto affected=[&](int i) {
    return InSubtree(i,hand,parents) || (fore_twist!=-1 &&
        (InSubtree(i,forearm,parents) || InSubtree(i,fore_twist,parents)));
  };
  const auto candidate=[&](int i) {
    if(InSubtree(i,hand,parents))return Rotate(poses[i],delta,pivot);
    if(InSubtree(i,forearm,parents))return Rotate(poses[i],pronation,elbow);
    return Rotate(poses[i],twist_skin,elbow);
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
                                       const ArmPoseLengths* calibrated_lengths = nullptr) {
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
  IkSolution solution = SolveTwoBoneIk(shoulder, wrist_target, pole, rig);
  if (!solution.valid || VLen(solution.upper_dir) < 0.99f ||
      VLen(solution.forearm_dir) < 0.99f) return result;
  Rotation upper_delta = FromTo(upper, Sub(solution.elbow, shoulder));
  Rotation forearm_delta = FromTo(upper_delta.Apply(forearm),
                                   Sub(solution.wrist, solution.elbow));
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
  result.reached = solution.reached;
  result.elbow = solution.elbow;
  result.wrist = solution.wrist;
  return result;
}

}  // namespace dsvr
