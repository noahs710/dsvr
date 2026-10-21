#include "../src/core/arm_pose_math.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
using namespace dsvr;

ArmPoseMatrix Pose(Vec3 position, float roll = 0, Vec3 scale = {1, 1, 1}) {
  float c = std::cos(roll), s = std::sin(roll);
  return {{c * scale.x, -s * scale.y, 0, position.x,
           s * scale.x, c * scale.y, 0, position.y,
           0, 0, scale.z, position.z}};
}

bool Near(float a, float b) { return std::fabs(a - b) < 2e-5f; }
bool Near(Vec3 a, Vec3 b) {
  return Near(a.x, b.x) && Near(a.y, b.y) && Near(a.z, b.z);
}
Vec3 Sub(Vec3 a, Vec3 b) { return VAdd(a, VScale(b, -1)); }
Vec3 Column(const ArmPoseMatrix& pose, int i) {
  return {pose.m[i], pose.m[4 + i], pose.m[8 + i]};
}

struct Fixture {
  // A bent unit-length chain, an upper-twist sibling and its child, hand
  // descendants with authored orientations/scales, plus unrelated bones.
  std::array<ArmPoseMatrix, 11> poses{
      Pose({3, 4, 5}, 0.3f, {2, 3, 4}), Pose({0, -0.1f, 0}),
      Pose({0, 0, 0}, 0.2f), Pose({0.6f, 0.8f, 0}, -0.4f),
      Pose({1.6f, 0.8f, 0}, 0.6f, {2, 3, 4}),
      Pose({1.8f, 0.9f, 0.2f}, 0.8f, {0.5f, 1, 2}),
      Pose({1.6f, 1.3f, 0}, -0.2f), Pose({0, 0, 0}, -0.7f),
      Pose({0.1f, 0.2f, 0.1f}, 0.1f), Pose({-1, 0, 0}),
      Pose({0, -1, 0})};
  std::array<int, 11> parents{-1, 0, 1, 2, 3, 4, 4, 1, 7, 1, 0};
  ArmPoseBones bones{2, 3, 4, 7};
};

}  // namespace

int main(int argc,char** argv) {
  using namespace dsvr;
  int failures = 0;
  auto check = [&](bool condition, const char* message) {
    if (!condition) { ++failures; std::printf("FAIL: %s\n", message); }
  };

  // An animation can bend the native elbow on either side while the physical
  // hand makes the same overhead motion. Runtime's anatomical pole must keep
  // that animation from reversing/suppressing the tracked upper-arm response.
  // --legacy-vertical intentionally exercises the previous bend-plane policy.
  {
    const bool legacy=argc>1 && std::strcmp(argv[1],"--legacy-vertical")==0;
    const Vec3 pole{.75f,-.3f,-.4f};
    Vec3 previous[2]{};
    for(int pose=0;pose<2;++pose) {
      Fixture f;
      f.poses[2]=Pose({0,0,0});
      f.poses[3]=Pose({pose?-.6f:.6f,.8f,0});
      f.poses[4]=Pose({0,1.6f,0});
      auto solved=SolveModelArmPose(f.poses,f.parents,f.bones,{0,1.4f,.35f},legacy?nullptr:&pole);
      check(solved.valid && solved.reached,"overhead trajectory remains reachable");
      previous[pose]=solved.elbow;
      auto raised=SolveModelArmPose(f.poses,f.parents,f.bones,{0,1.6f,.35f},legacy?nullptr:&pole);
      check(raised.valid && raised.reached && raised.elbow.y-solved.elbow.y>.03f,
            "small vertical hand motion raises the elbow/upper arm");
    }
    check(Near(previous[0],previous[1]),"native animation changes cannot redefine overhead elbow bend plane");
    if(legacy)return failures?1:0;
  }

  // Reach and elbow placement use the actual measured model-space links.
  {
    Fixture f;
    auto before = f.poses;
    Vec3 target{0.8f, 1, 0.4f};
    ArmPoseResult result = SolveModelArmPose(f.poses, f.parents, f.bones, target);
    check(result.valid && result.reached, "reachable target accepted");
    Vec3 shoulder = ArmPoseOrigin(f.poses[2]);
    Vec3 elbow = ArmPoseOrigin(f.poses[3]);
    Vec3 wrist = ArmPoseOrigin(f.poses[4]);
    check(Near(wrist, target) && Near(wrist, result.wrist), "wrist reaches target");
    check(Near(elbow, result.elbow), "forearm origin is solved elbow");
    check(Near(VLen(Sub(elbow, shoulder)), 1) &&
          Near(VLen(Sub(wrist, elbow)), 1), "both link lengths retained");
    Vec3 pole{-0.2f, 0.4f, 0};
    Vec3 direction = VNorm(Sub(target, shoulder));
    Vec3 bend = Sub(elbow, VScale(direction, VDot(elbow, direction)));
    check(VDot(bend, pole) > 0, "existing elbow bend side retained");
    for (int index : {0, 1, 9, 10})
      check(std::memcmp(&f.poses[index], &before[index], sizeof(ArmPoseMatrix)) == 0,
            "root, torso and unrelated limbs byte-preserved");
    for (size_t i = 0; i < f.poses.size(); ++i)
      for (int a = 0; a < 3; ++a)
        for (int b = 0; b < 3; ++b)
          check(Near(VDot(Column(f.poses[i], a), Column(f.poses[i], b)),
                     VDot(Column(before[i], a), Column(before[i], b))),
                "rigid deltas preserve every basis scale and angle");

    // Position and orientation relative to the hand stay fixed, even with
    // nonuniform bone scale. This carries both fingers and weapon mounts.
    for (int child : {5, 6}) {
      for (int axis = -1; axis < 3; ++axis) {
        Vec3 old_point = ArmPoseOrigin(before[child]);
        Vec3 new_point = ArmPoseOrigin(f.poses[child]);
        if (axis >= 0) {
          old_point = VAdd(old_point, Column(before[child], axis));
          new_point = VAdd(new_point, Column(f.poses[child], axis));
        }
        Vec3 old_local{}, new_local{};
        check(ArmPoseInversePoint(before[4], old_point, old_local) &&
              ArmPoseInversePoint(f.poses[4], new_point, new_local) &&
              Near(old_local, new_local), "hand descendants retain authored stance");
      }
    }
    // The twist sibling receives exactly the same upper-arm rigid delta.
    Vec3 old_local{}, new_local{};
    check(ArmPoseInversePoint(before[2], ArmPoseOrigin(before[8]), old_local) &&
          ArmPoseInversePoint(f.poses[2], ArmPoseOrigin(f.poses[8]), new_local) &&
          Near(old_local, new_local), "upper twist branch follows upper arm");
  }

  // A target beyond maximum reach straightens, with no stretching.
  {
    Fixture f;
    auto result = SolveModelArmPose(f.poses, f.parents, f.bones, {0, 0, 20});
    check(result.valid && !result.reached && Near(result.wrist, {0, 0, 2}),
          "far target clamps to maximum reach");
    check(Near(ArmPoseOrigin(f.poses[3]), {0, 0, 1}) &&
          Near(ArmPoseOrigin(f.poses[4]), {0, 0, 2}), "far clamp updates both joints");
  }

  // Unequal links have an inner reach limit; equal links may fold at shoulder.
  {
    Fixture f;
    f.poses[3] = Pose({1, 0, 0});
    f.poses[4] = Pose({1.4f, 0, 0});
    auto result = SolveModelArmPose(f.poses, f.parents, f.bones, {0.1f, 0, 0});
    check(result.valid && !result.reached && Near(result.wrist, {0.6f, 0, 0}),
          "near target clamps to link length difference");
    check(Near(VLen(Sub(ArmPoseOrigin(f.poses[4]), ArmPoseOrigin(f.poses[3]))),
               0.4f), "inner clamp preserves forearm length");
    f = Fixture{};
    result = SolveModelArmPose(f.poses, f.parents, f.bones, {0, 0, 0});
    check(result.valid && result.reached && VLen(result.wrist) < 2e-5f,
          "equal links fold onto shoulder without nonfinite pose");
  }

  // A straight chain rotated 90 degrees retains authored twist. The unchanged
  // Z basis tests the minimal axis rotation, rather than a look-at rebuild.
  {
    Fixture f;
    f.poses[2] = Pose({0, 0, 0});
    f.poses[3] = Pose({1, 0, 0});
    f.poses[4] = Pose({2, 0, 0});
    auto result = SolveModelArmPose(f.poses, f.parents, f.bones, {0, 2, 0});
    check(result.valid && Near(Column(f.poses[2], 0), {0, 1, 0}) &&
          Near(Column(f.poses[2], 2), {0, 0, 1}), "shortest-arc aim preserves twist");
    result = SolveModelArmPose(f.poses, f.parents, f.bones, {0, -2, 0});
    check(result.valid && Near(ArmPoseOrigin(f.poses[4]), {0, -2, 0}),
          "opposite direction produces a finite 180 degree solve");
  }

  // Reusing the current target does not change the authored pose. Omitting
  // the optional twist branch must leave that sibling byte-for-byte intact.
  {
    Fixture f;
    auto before = f.poses;
    auto result = SolveModelArmPose(f.poses, f.parents, f.bones,
                                    ArmPoseOrigin(f.poses[4]));
    check(result.valid, "current wrist target accepted");
    for (size_t i = 0; i < f.poses.size(); ++i)
      for (int j = 0; j < 12; ++j)
        check(Near(f.poses[i].m[j], before[i].m[j]), "current target preserves pose");
    f = Fixture{};
    f.bones.upper_twist = -1;
    result = SolveModelArmPose(f.poses, f.parents, f.bones, {0, 0, 2});
    check(result.valid && std::memcmp(&f.poses[7], &before[7], sizeof(ArmPoseMatrix)) == 0 &&
          std::memcmp(&f.poses[8], &before[8], sizeof(ArmPoseMatrix)) == 0,
          "unselected twist sibling and descendants untouched");
  }

  // Hierarchy order is immaterial: parent indices may follow their children.
  {
    std::array<ArmPoseMatrix, 4> poses{
        Pose({2, 0, 0}), Pose({0, 0, 0}), Pose({1, 0, 0}), Pose({0, -1, 0})};
    std::array<int, 4> parents{2, 3, 1, -1};
    auto result = SolveModelArmPose(poses, parents, {1, 2, 0, -1}, {0, 2, 0});
    check(result.valid && Near(ArmPoseOrigin(poses[0]), {0, 2, 0}),
          "unordered parent array supported");
  }

  // All refusal paths are transactional, including malformed unrelated bones.
  for (int scenario = 0; scenario < 13; ++scenario) {
    Fixture f;
    Vec3 target{0, 0, 1};
    switch (scenario) {
      case 0: f.parents[9] = 99; break;
      case 1: f.parents[0] = 10; break;  // unrelated root/leg cycle
      case 2: f.parents[3] = 1; break;   // wrong direct chain
      case 3: f.bones.hand = 99; break;
      case 4: f.bones.upper_twist = 2; break;
      case 5: f.poses[10].m[11] = std::numeric_limits<float>::quiet_NaN(); break;
      case 6: target.x = std::numeric_limits<float>::infinity(); break;
      case 7: f.poses[4] = f.poses[3]; break;  // zero forearm length
      case 8: f.poses[2].m[0] = 0; f.poses[2].m[4] = 0; break;
      case 9: f.poses[5].m[1] += 0.2f; break;  // sheared finger basis
      case 10: f.bones.upper_twist = 3; break;  // not an upper sibling
      case 11: f.bones.upper_twist = -2; break;
      case 12: f.poses[3].m[3] = 1e30f; break;  // link-length overflow
    }
    auto before = f.poses;
    auto result = SolveModelArmPose(f.poses, f.parents, f.bones, target);
    check(!result.valid && std::memcmp(f.poses.data(), before.data(), sizeof(before)) == 0,
          "invalid solve refuses without partial writes");
  }
  {
    Fixture f;
    auto before = f.poses;
    check(!SolveModelArmPose(f.poses, std::span<const int>(f.parents).first(10),
                             f.bones, {0, 0, 1}).valid &&
          std::memcmp(f.poses.data(), before.data(), sizeof(before)) == 0,
          "mismatched bounded spans refused");
    check(!SolveModelArmPose({}, {}, {}, {0, 0, 0}).valid, "empty pose refused");
  }

  // World/model mapping uses the verified affine layout and accounts for
  // rotation, translation, nonuniform scale and reflected axes.
  {
    ArmPoseMatrix root = Pose({-10, 6, 23}, 0.7f, {2, 3, -4});
    Vec3 model{0.3f, 1.4f, -0.5f}, recovered{};
    Vec3 world = ArmPoseTransformPoint(root, model);
    check(ArmPoseInversePoint(root, world, recovered) && Near(model, recovered),
          "affine world/model point round trip");
    root.m[1] += 0.3f;
    recovered = {7, 8, 9};
    check(!ArmPoseInversePoint(root, world, recovered) && Near(recovered, {7, 8, 9}),
          "invalid inverse leaves result unchanged");
  }

  // Forearm-twist bone (parented to the upper arm, skins the forearm) must
  // follow the forearm delta, not just the upper delta, or its verts shear.
  {
    Fixture f;
    f.parents[10] = 2;
    ArmPoseBones with_fore{2, 3, 4, 7, -1, 10};
    auto before = f.poses;
    ArmPoseResult result =
        SolveModelArmPose(f.poses, f.parents, with_fore, {0.8f, 1, 0.4f});
    check(result.valid, "forearm-twist solve accepted");
    check(Near(VLen(Sub(ArmPoseOrigin(f.poses[10]),
                        ArmPoseOrigin(f.poses[3]))),
               VLen(Sub(ArmPoseOrigin(before[10]),
                        ArmPoseOrigin(before[3])))) &&
              Near(VLen(Sub(ArmPoseOrigin(f.poses[10]),
                            ArmPoseOrigin(f.poses[4]))),
                   VLen(Sub(ArmPoseOrigin(before[10]),
                            ArmPoseOrigin(before[4])))),
          "forearm-twist keeps rigid distances to elbow and wrist");
  }

  // Grip roll: basis rotates around the grip axis, axis itself unchanged.
  {
    ArmPoseMatrix identity{};
    identity.m[0] = identity.m[5] = identity.m[10] = 1;
    ArmPoseMatrix rolled{};
    check(ArmPoseRollBasis(identity, {1, 0, 0}, 1.5707963f, rolled),
          "grip roll accepts unit basis");
    Vec3 col1{rolled.m[1], rolled.m[5], rolled.m[9]};
    Vec3 col2{rolled.m[2], rolled.m[6], rolled.m[10]};
    check(Near(col1, {0, 0, 1}) && Near(col2, {0, -1, 0}),
          "grip roll rotates palm axes around grip axis");
    check(!ArmPoseRollBasis(identity, {0, 0, 0}, 1.0f, rolled),
          "grip roll refuses zero axis");
  }

  // A reflected controller frame still produces a proper wrist rotation.
  // Rotate the controller +90 degrees about tracking Z without moving it.
  {
    Fixture f;
    const auto before=f.poses;
    const auto reference=Pose({},0,{-1,1,1});
    const ArmPoseMatrix controller{{0,1,0,0,1,0,0,0,0,0,1,0}};
    ArmPoseMatrix desired{};
    check(ArmPoseGripTarget(controller,reference,before[4],desired),
          "reflected calibrated controller produces wrist target");
    check(OrientModelHandPose(f.poses,f.parents,4,desired), "tracked wrist rotation accepted");
    check(Near(ArmPoseOrigin(f.poses[4]),ArmPoseOrigin(before[4])),
          "wrist rotation cannot move the solved wrist");
    for (int axis=0;axis<3;++axis) {
      const auto v=Column(before[4],axis);
      check(Near(Column(f.poses[4],axis),{v.y,-v.x,v.z}),
            "positive tracking roll maps through calibrated handedness");
    }
    for (int index:{0,1,2,3,7,8,9,10})
      check(std::memcmp(&f.poses[index],&before[index],sizeof(ArmPoseMatrix))==0,
            "wrist orientation leaves root, elbow and unrelated branches unchanged");
    for (int child:{5,6}) {
      for (int axis=-1;axis<3;++axis) {
        auto old_point=ArmPoseOrigin(before[child]);
        auto new_point=ArmPoseOrigin(f.poses[child]);
        if (axis>=0) {
          old_point=VAdd(old_point,Column(before[child],axis));
          new_point=VAdd(new_point,Column(f.poses[child],axis));
        }
        Vec3 old_local{},new_local{};
        check(ArmPoseInversePoint(before[4],old_point,old_local) &&
              ArmPoseInversePoint(f.poses[4],new_point,new_local) && Near(old_local,new_local),
              "wrist carries native fingers/equipment without changing their grip");
      }
    }
    const auto rotated=f.poses;
    desired.m[0]=-desired.m[0];desired.m[4]=-desired.m[4];desired.m[8]=-desired.m[8];
    check(!OrientModelHandPose(f.poses,f.parents,4,desired) &&
          std::memcmp(f.poses.data(),rotated.data(),sizeof(rotated))==0,
          "reflected wrist output is refused transactionally");
    f.parents[1]=4;
    check(!OrientModelHandPose(f.poses,f.parents,4,Pose({})),
          "wrist correction refuses cyclic hierarchy");
    ArmPoseMatrix invalid{};
    const auto previous=desired;
    check(!ArmPoseGripTarget(controller,invalid,before[4],desired) &&
          std::memcmp(&previous,&desired,sizeof(desired))==0,
          "bad calibration refuses without overwriting target");
  }

  // Grip compensation: the wrist is offset so the weapon/grip point, not
  // the wrist joint, lands on the controller grip pose.
  {
    const auto reference_hand = Pose({1, 2, 3}, 0, {2, 3, 4});
    ArmPoseMatrix reference_weapon = reference_hand;
    reference_weapon.m[3] += 0.2f;  // grip 0.2 along model X from the wrist
    const ArmPoseMatrix desired{{0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0}};
    Vec3 wrist{9, 9, 9};
    check(ArmPoseWristForGrip({5, 5, 5}, desired, reference_hand,
                              reference_weapon, wrist),
          "grip compensation accepts calibrated bases");
    check(Near(wrist, {5, 4.8f, 5}),
          "rotated grip offset moves the wrist opposite the grip");
    ArmPoseMatrix identity{};
    identity.m[0] = identity.m[5] = identity.m[10] = 1;
    check(ArmPoseWristForGrip({5, 5, 5}, identity, reference_hand,
                              reference_weapon, wrist) &&
              Near(wrist, {4.8f, 5, 5}),
          "unrotated grip offset subtracts along the hand axis");
    ArmPoseMatrix reflected = desired;
    reflected.m[0] = -reflected.m[0];
    reflected.m[4] = -reflected.m[4];
    reflected.m[8] = -reflected.m[8];
    wrist = {7, 8, 9};
    check(!ArmPoseWristForGrip({5, 5, 5}, reflected, reference_hand,
                               reference_weapon, wrist) &&
              Near(wrist, {7, 8, 9}),
          "reflected grip target refused without overwriting wrist");
    ArmPoseMatrix invalid{};
    check(!ArmPoseWristForGrip({5, 5, 5}, desired, invalid,
                               reference_weapon, wrist) &&
              Near(wrist, {7, 8, 9}),
          "bad grip calibration refused without overwriting wrist");
  }

  // A stationary grip can pronate without swinging either link. Rotating only
  // the hand leaves the skinned forearm fixed and pinches the wrist seam.
  for(float angle:{-1.5707963f,1.5707963f,3.14159265f}) {
    std::array<ArmPoseMatrix,5> poses{Pose({0,0,0}),Pose({0,1,0}),
        Pose({0,2,0}),Pose({0,1.5f,0}),Pose({0,2,.1f})};
    const auto before=poses;
    const std::array<int,5> parents{-1,0,1,0,2};
    ArmPoseMatrix desired{{std::cos(angle),0,std::sin(angle),0,0,1,0,0,
                          -std::sin(angle),0,std::cos(angle),0}};
    check(OrientModelHandPose(poses,parents,2,desired,3),"physical forearm roll accepted");
    check(Near(Column(poses[1],0),{std::cos(angle),0,-std::sin(angle)}),
          "controller roll rotates forearm instead of pinching only the wrist");
    check(std::fabs(Column(poses[3],0).z)>.5f,"forearm twist skin follows pronation");
    for(int bone:{0,1,2,3})check(Near(ArmPoseOrigin(poses[bone]),ArmPoseOrigin(before[bone])),
        "pronation preserves root and elbow/wrist/twist positions");
    for(int axis=0;axis<3;++axis)check(Near(Column(poses[2],axis),Column(desired,axis)),
        "hand still reaches the complete requested orientation");
  }
  // Wrist flexion has no axial component, including the ambiguous half-turn.
  for(float angle:{.6f,3.14159265f}) {
    std::array<ArmPoseMatrix,5> poses{Pose({}),Pose({0,1,0}),
        Pose({0,2,0}),Pose({0,1,0}),Pose({0,2,.1f})};
    const auto before=poses;
    const std::array<int,5> parents{-1,0,1,0,2};
    ArmPoseMatrix desired{{1,0,0,0,0,std::cos(angle),-std::sin(angle),0,
                          0,std::sin(angle),std::cos(angle),0}};
    check(OrientModelHandPose(poses,parents,2,desired,3),"wrist flexion accepted");
    for(int bone:{0,1,3})for(int axis=0;axis<3;++axis)
      check(Near(Column(poses[bone],axis),Column(before[bone],axis)),
            "pure wrist flexion cannot roll the forearm or twist skin");
  }
  // Native reflected/scaled poses retain their parity and authored scales.
  {
    std::array<ArmPoseMatrix,5> poses{Pose({}),Pose({0,1,0},0,{-2,3,4}),
        Pose({0,2,0},0,{-2,3,4}),Pose({0,1,0},0,{-2,3,4}),Pose({0,2,.1f})};
    const std::array<int,5> parents{-1,0,1,0,2};
    ArmPoseMatrix desired{{0,0,4,0,0,3,0,0,2,0,0,0}};
    check(OrientModelHandPose(poses,parents,2,desired,3),"reflected native pronation accepted");
    check(Near(Column(poses[1],0),{0,0,2}) && Near(Column(poses[1],2),{4,0,0}),
          "forearm pronation preserves reflected basis and nonuniform scale");
    auto before=poses;
    poses[3]={};before=poses;
    check(!OrientModelHandPose(poses,parents,2,desired,3) &&
        std::memcmp(poses.data(),before.data(),sizeof(poses))==0,
        "invalid twist skin refuses the whole orientation transaction");
    poses=before;poses[3]=Pose({0,1,0});
    auto bad_parents=parents;bad_parents[3]=2;before=poses;
    check(!OrientModelHandPose(poses,bad_parents,2,desired,3) &&
        std::memcmp(poses.data(),before.data(),sizeof(poses))==0,
        "twist bone in the wrong branch refuses without modifying the hand");
  }
  std::printf("arm pose math: %d failures\n", failures);
  return failures ? 1 : 0;
}
