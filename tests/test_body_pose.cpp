// Offline body-pose tests: torso yaw retargeting math. CHECK executes even
// under NDEBUG. Fixtures are synthetic model-space pose palettes; no game,
// no native memory, no OpenXR.
#include "../src/core/body_pose_math.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
using namespace dsvr;

int g_checks = 0;
int g_failures = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    ++g_checks;                                                         \
    if (!(cond)) {                                                      \
      ++g_failures;                                                     \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
    }                                                                   \
  } while (0)

bool Near(float a, float b, float eps = 1e-4f) {
  return std::fabs(a - b) < eps;
}
bool VNear(Vec3 v, Vec3 b, float eps = 1e-4f) {
  return Near(v.x, b.x, eps) && Near(v.y, b.y, eps) && Near(v.z, b.z, eps);
}
Vec3 Sub(Vec3 a, Vec3 b) { return VAdd(a, VScale(b, -1)); }

ArmPoseMatrix Pose(Vec3 position) {
  return {{1, 0, 0, position.x, 0, 1, 0, position.y, 0, 0, 1, position.z}};
}

// 0 root, 1 spine, 2 chest, 3 neck, 4 head, 5..7 left arm, 8..10 right
// arm, 11 pelvis (outside the spine subtree).
struct Rig {
  std::array<ArmPoseMatrix, 12> poses{
      Pose({0, 0, 0}),    Pose({0, 1.0f, 0}),   Pose({0, 1.2f, 0}),
      Pose({0, 1.45f, 0}), Pose({0, 1.6f, 0}),  Pose({-0.2f, 1.35f, 0}),
      Pose({-0.5f, 1.35f, 0}), Pose({-0.75f, 1.35f, 0}),
      Pose({0.2f, 1.35f, 0}),  Pose({0.5f, 1.35f, 0}),
      Pose({0.75f, 1.35f, 0}), Pose({0, 0.9f, 0})};
  std::array<int, 12> parents{-1, 0, 1, 2, 3, 2, 5, 6, 2, 8, 9, 0};
};

// Model->world root with translation, yawed 180 degrees so the model's
// -Z facing lands on world +Z. Caller zeroes translation before solving.
ArmPoseMatrix RootYawPi(Vec3 t = {10, 0, 10}) {
  return {{-1, 0, 0, t.x, 0, 1, 0, t.y, 0, 0, -1, t.z}};
}
// Same facing with a reflected (negative-determinant) root basis.
ArmPoseMatrix RootReflected(Vec3 t = {10, 0, 10}) {
  return {{-1, 0, 0, t.x, 0, 1, 0, t.y, 0, 0, 1, t.z}};
}
Vec3 WorldPoint(const ArmPoseMatrix& root, Vec3 model) {
  return ArmPoseTransformPoint(root, model);
}

void CheckTorsoContracts() {
  const Vec3 right{1,0,0},up{0,1,0},forward{0,0,1};
  const float turn=2.4f;
  const Vec3 heading_f{std::sin(turn),0,std::cos(turn)};
  const Vec3 heading_r{std::cos(turn),0,-std::sin(turn)};
  const auto facing=[](const Rig& rig,int bone,const ArmPoseMatrix& root) {
    ArmPoseMatrix basis=root;basis.m[3]=basis.m[7]=basis.m[11]=0;
    return VNorm(VScale(ArmPoseTransformPoint(basis,
        arm_pose_detail::Column(rig.poses[bone],2)),-1));
  };
  {
    Rig rig;float applied=0;
    CHECK(SolveTorsoYaw(rig.poses,rig.parents,1,RootYawPi(),
        heading_r,heading_f,heading_f,applied));
    const float actual=RelativeYaw(facing(rig,1,RootYawPi()),right,forward);
    CHECK(Near(actual,kTorsoCorrectionLimitRad,1e-3f));
    CHECK(Near(actual,applied,1e-3f));
  }
  const TorsoBones bones{-1,1,2,3,4};
  {
    Rig rig;TorsoPoseResult out;
    CHECK(SolveTorsoPose(rig.poses,rig.parents,bones,RootYawPi(),
        heading_r,heading_f,0,0,right,{0,0,-1},up,out));
    const float actual=RelativeYaw(facing(rig,3,RootYawPi()),right,forward);
    CHECK(Near(actual,kTorsoCorrectionLimitRad,1e-3f));
    CHECK(Near(actual,out.applied_delta,1e-3f));
  }
  {
    Rig rig;const auto before=rig.poses;TorsoPoseResult out;
    CHECK(!SolveTorsoPose(rig.poses,rig.parents,bones,RootYawPi(),
        right,forward,.5f,0,right,up,{},out));
    CHECK(!out.valid && std::memcmp(rig.poses.data(),before.data(),sizeof(before))==0);
    auto disconnected=rig.parents;disconnected[2]=0;
    CHECK(!SolveTorsoPose(rig.poses,disconnected,bones,RootYawPi(),
        right,forward,.5f,0,right,up,forward,out));
    CHECK(std::memcmp(rig.poses.data(),before.data(),sizeof(before))==0);
  }
  {
    Rig rig;const auto before=rig.poses;float applied=0;
    CHECK(SolveTorsoLean(rig.poses,rig.parents,bones,RootYawPi(),{.1f,0,.05f},applied));
    CHECK(VNear(ArmPoseOrigin(rig.poses[1]),ArmPoseOrigin(before[1])));
    for (int bone:{2,3,4,5,6,7,8,9,10}) {
      const int parent=rig.parents[bone];
      CHECK(Near(VLen(Sub(ArmPoseOrigin(rig.poses[bone]),ArmPoseOrigin(rig.poses[parent]))),
          VLen(Sub(ArmPoseOrigin(before[bone]),ArmPoseOrigin(before[parent]))),1e-3f));
    }
  }

  // Combined reaches/look directions retain every authored link under scaled/reflected roots.
  for (bool reflected:{false,true}) for (float scale:{.5f,1.f,2.f})
    for (float yaw:{-2.8f,-.4f,0.f,.4f,2.8f}) for (float pitch:{-.7f,0.f,.7f}) {
      Rig rig;const auto before=rig.poses;
      auto root=reflected?RootReflected():RootYawPi();
      for (int row=0;row<3;++row) for (int column=0;column<3;++column)
        root.m[row*4+column]*=scale;
      float lean=0;
      CHECK(SolveTorsoLean(rig.poses,rig.parents,bones,root,{.10f,-.05f,.08f},lean));
      CHECK(std::isfinite(lean) && lean<=kTorsoLeanLimitM+1e-3f);
      const Vec3 camera_r{std::cos(yaw),0,-std::sin(yaw)};
      const Vec3 camera_f{std::sin(yaw)*std::cos(pitch),std::sin(pitch),std::cos(yaw)*std::cos(pitch)};
      TorsoPoseResult out;
      CHECK(SolveTorsoPose(rig.poses,rig.parents,bones,root,right,forward,yaw,
          TorsoPitchShare(camera_f),camera_r,VCross(camera_f,camera_r),camera_f,out));
      CHECK(out.valid && std::fabs(out.applied_delta)<=kTorsoCorrectionLimitRad+1e-4f);
      for (std::size_t bone=1;bone<rig.poses.size();++bone) {
        const int parent=rig.parents[bone];
        CHECK(arm_pose_detail::ValidMatrix(rig.poses[bone]));
        CHECK(Near(VLen(Sub(ArmPoseOrigin(rig.poses[bone]),ArmPoseOrigin(rig.poses[parent]))),
            VLen(Sub(ArmPoseOrigin(before[bone]),ArmPoseOrigin(before[parent]))),1e-3f));
      }
      CHECK(std::memcmp(&rig.poses[0],&before[0],sizeof(ArmPoseMatrix))==0);
      CHECK(std::memcmp(&rig.poses[11],&before[11],sizeof(ArmPoseMatrix))==0);
    }
  for (Vec3 lean:{Vec3{0,-.3f,0},Vec3{0,.3f,0},Vec3{100,0,0}}) {
    Rig rig;const auto before=rig.poses;float applied=0;
    CHECK(SolveTorsoLean(rig.poses,rig.parents,bones,RootYawPi(),lean,applied));
    CHECK(std::isfinite(applied) && applied<=kTorsoLeanLimitM+1e-3f);
    CHECK(VNear(ArmPoseOrigin(rig.poses[1]),ArmPoseOrigin(before[1])));
    for (int bone:{2,3}) CHECK(Near(VLen(Sub(ArmPoseOrigin(rig.poses[bone]),
        ArmPoseOrigin(rig.poses[rig.parents[bone]]))),VLen(Sub(ArmPoseOrigin(before[bone]),
        ArmPoseOrigin(before[rig.parents[bone]]))),1e-3f));
  }

  // The comfort budget stays in physical meters when world scale changes.
  for (float units:{.5f,1.f,2.f}) {
    Rig rig;auto root=RootYawPi();
    for (int row=0;row<3;++row) for (int column=0;column<3;++column) root.m[row*4+column]*=units;
    float applied=0;
    CHECK(SolveTorsoLean(rig.poses,rig.parents,bones,root,{.4f*units,0,0},applied,units));
    CHECK(applied>.15f && applied<=kTorsoLeanLimitM+1e-3f);
  }
  for (float units:{0.f,-1.f,std::numeric_limits<float>::quiet_NaN()}) {
    Rig rig;const auto before=rig.poses;float applied=1;
    CHECK(!SolveTorsoLean(rig.poses,rig.parents,bones,RootYawPi(),{.1f,0,0},applied,units));
    CHECK(applied==0 && std::memcmp(rig.poses.data(),before.data(),sizeof(before))==0);
  }
}
}  // namespace

int main() {
  using namespace dsvr;
  const Vec3 kRight{1, 0, 0}, kUp{0, 1, 0}, kFwd{0, 0, 1};

  // Relative yaw follows the camera convention: toward +right is positive.
  {
    CHECK(Near(RelativeYaw(kFwd, kRight, kFwd), 0.0f));
    CHECK(Near(RelativeYaw(kRight, kRight, kFwd), 1.5707963f));
    CHECK(Near(RelativeYaw(VScale(kRight, -1), kRight, kFwd), -1.5707963f));
    Vec3 behind = VScale(kFwd, -1);
    CHECK(Near(std::fabs(RelativeYaw(behind, kRight, kFwd)), 3.1415927f));
    CHECK(std::isfinite(RelativeYaw({0, 1, 0}, kRight, kFwd)));
  }

  // Subtree rotation moves the spine descendants rigidly and nothing else.
  {
    Rig rig;
    const auto before = rig.poses;
    CHECK(RotatePoseSubtree(rig.poses, rig.parents, 1, kUp,
                            ArmPoseOrigin(rig.poses[1]), 1.5707963f));
    // Left hand (-0.75, 1.35, 0) about the spine pivot (0,1,0): +90 deg
    // about +Y maps (x,z)=(-0.75,0) to (0, +0.75).
    CHECK(VNear(ArmPoseOrigin(rig.poses[7]), {0, 1.35f, 0.75f}));
    CHECK(VNear(ArmPoseOrigin(rig.poses[10]), {0, 1.35f, -0.75f}));
    CHECK(Near(VLen(Sub(ArmPoseOrigin(rig.poses[7]),
                        ArmPoseOrigin(rig.poses[6]))),
               VLen(Sub(ArmPoseOrigin(before[7]), ArmPoseOrigin(before[6])))));
    CHECK(Near(VLen(Sub(ArmPoseOrigin(rig.poses[6]),
                        ArmPoseOrigin(rig.poses[5]))),
               VLen(Sub(ArmPoseOrigin(before[6]), ArmPoseOrigin(before[5])))));
    for (int outside : {0, 11})
      CHECK(std::memcmp(&rig.poses[outside], &before[outside],
                        sizeof(ArmPoseMatrix)) == 0);
    // Zero angle is a no-op success.
    Rig still;
    const auto still_before = still.poses;
    CHECK(RotatePoseSubtree(still.poses, still.parents, 1, kUp,
                            ArmPoseOrigin(still.poses[1]), 0.0f));
    CHECK(std::memcmp(still.poses.data(), still_before.data(),
                      sizeof(still.poses)) == 0);
  }

  // Malformed input refuses atomically.
  {
    Rig rig;
    const auto before = rig.poses;
    CHECK(!RotatePoseSubtree(rig.poses, rig.parents, 1, {0, 0, 0},
                             {0, 1, 0}, 0.5f));
    CHECK(!RotatePoseSubtree(rig.poses, rig.parents, 1, kUp, {0, 1, 0},
                             std::numeric_limits<float>::quiet_NaN()));
    CHECK(!RotatePoseSubtree(rig.poses, rig.parents, 99, kUp, {0, 1, 0}, 0.5f));
    CHECK(!RotatePoseSubtree(rig.poses, rig.parents, -1, kUp, {0, 1, 0}, 0.5f));
    auto cyclic = rig.parents;
    cyclic[1] = 2;  // spine <-> chest cycle
    CHECK(!RotatePoseSubtree(rig.poses, cyclic, 1, kUp, {0, 1, 0}, 0.5f));
    auto bad_parent = rig.parents;
    bad_parent[3] = 42;
    CHECK(!RotatePoseSubtree(rig.poses, bad_parent, 1, kUp, {0, 1, 0}, 0.5f));
    CHECK(std::memcmp(rig.poses.data(), before.data(), sizeof(rig.poses)) == 0);
  }

  // Head aligned with a native facing that already matches the heading:
  // no correction, poses untouched.
  {
    Rig rig;
    const auto before = rig.poses;
    float applied = -99.0f;
    CHECK(SolveTorsoYaw(rig.poses, rig.parents, 1, RootYawPi(), kRight, kFwd,
                        kFwd, applied));
    CHECK(Near(applied, 0.0f));
    CHECK(std::memcmp(rig.poses.data(), before.data(), sizeof(rig.poses)) == 0);
  }

  // Head turned +0.5 rad toward +right: shoulders follow the head, pelvis
  // and link lengths stay put, and the world-space shoulder swing matches.
  {
    Rig rig;
    const Vec3 head_f = VAdd(VScale(kFwd, std::cos(0.5f)),
                             VScale(kRight, std::sin(0.5f)));
    float applied = 0.0f;
    CHECK(SolveTorsoYaw(rig.poses, rig.parents, 1, RootYawPi(), kRight, kFwd,
                        head_f, applied));
    CHECK(Near(applied, 0.5f, 1e-3f));
    const ArmPoseMatrix root = RootYawPi();
    const Vec3 shoulder_w =
        WorldPoint(root, ArmPoseOrigin(rig.poses[5]));
    const Vec3 spine_w = WorldPoint(root, ArmPoseOrigin(rig.poses[1]));
    CHECK(Near(RelativeYaw(VNorm(Sub(shoulder_w, spine_w)), kRight, kFwd),
               RelativeYaw(VNorm(Vec3{0.2f, 0, 0}), kRight, kFwd) + 0.5f,
               1e-3f));
    CHECK(VNear(ArmPoseOrigin(rig.poses[11]), {0, 0.9f, 0}));
    CHECK(Near(VLen(Sub(ArmPoseOrigin(rig.poses[7]),
                        ArmPoseOrigin(rig.poses[5]))),
               0.55f));
  }

  // A 120-degree head turn exceeds the anatomical twist bound: the torso
  // follows only up to the limit instead of winding the spine around.
  {
    Rig rig;
    const float turn = 2.0943951f;
    const Vec3 head_f = VAdd(VScale(kFwd, std::cos(turn)),
                             VScale(kRight, std::sin(turn)));
    float applied = 0.0f;
    CHECK(SolveTorsoYaw(rig.poses, rig.parents, 1, RootYawPi(), kRight, kFwd,
                        head_f, applied));
    CHECK(Near(applied, kTorsoTwistLimitRad, 1e-3f));
  }

  // Native animation facing away from the heading is corrected even when
  // the head looks along the heading: the torso re-faces the body frame.
  {
    Rig rig;
    // Heading turned -0.3 rad from the native facing (native faces +Z).
    const float yaw = -0.3f;
    const Vec3 hr{std::cos(yaw), 0, -std::sin(yaw)};
    const Vec3 hf{std::sin(yaw), 0, std::cos(yaw)};
    float applied = 0.0f;
    CHECK(SolveTorsoYaw(rig.poses, rig.parents, 1, RootYawPi(), hr, hf, hf,
                        applied));
    CHECK(Near(applied, yaw, 1e-3f));
  }

  // Reflected model roots (mirrored basis) turn the same world direction.
  {
    Rig rig;
    const Vec3 head_f = VAdd(VScale(kFwd, std::cos(0.4f)),
                             VScale(kRight, std::sin(0.4f)));
    float applied = 0.0f;
    CHECK(SolveTorsoYaw(rig.poses, rig.parents, 1, RootReflected(), kRight,
                        kFwd, head_f, applied));
    CHECK(Near(applied, 0.4f, 1e-3f));
    const ArmPoseMatrix root = RootReflected();
    const Vec3 shoulder_w =
        WorldPoint(root, ArmPoseOrigin(rig.poses[8]));
    const Vec3 spine_w = WorldPoint(root, ArmPoseOrigin(rig.poses[1]));
    Rig fresh;
    const float before_yaw = RelativeYaw(
        VNorm(Sub(WorldPoint(root, ArmPoseOrigin(fresh.poses[8])),
                  WorldPoint(root, ArmPoseOrigin(fresh.poses[1])))),
        kRight, kFwd);
    CHECK(Near(RelativeYaw(VNorm(Sub(shoulder_w, spine_w)), kRight, kFwd),
               before_yaw + 0.4f, 1e-3f));
  }

  // Native facing far from the heading (locomotion facing, not a head
  // turn) must not wind the torso across the camera: the correction is
  // bounded by the same anatomical twist limit (live report: torso yaw
  // correction saturated at 1.2 rad and the camera ended up in the chest).
  {
    Rig rig;
    const float yaw = 2.5f;
    const Vec3 hr{std::cos(yaw), 0, -std::sin(yaw)};
    const Vec3 hf{std::sin(yaw), 0, std::cos(yaw)};
    float applied = 0.0f;
    CHECK(SolveTorsoYaw(rig.poses, rig.parents, 1, RootYawPi(), hr, hf, hf,
                        applied));
    CHECK(Near(applied, kTorsoCorrectionLimitRad, 1e-3f));
    CHECK(kTorsoCorrectionLimitRad <= kTorsoTwistLimitRad);
  }

  // Degenerate geometry refuses without touching the palette.
  {
    Rig rig;
    const auto before = rig.poses;
    float applied = 7.0f;
    CHECK(!SolveTorsoYaw(rig.poses, rig.parents, 1, RootYawPi(), kRight, kFwd,
                         {0, 0, 0}, applied));
    CHECK(!SolveTorsoYaw(rig.poses, rig.parents, 77, RootYawPi(), kRight, kFwd,
                         kFwd, applied));
    ArmPoseMatrix zero{};
    CHECK(!SolveTorsoYaw(rig.poses, rig.parents, 1, zero, kRight, kFwd, kFwd,
                         applied));
    CHECK(std::memcmp(rig.poses.data(), before.data(), sizeof(rig.poses)) == 0);
  }

  // Lean solves the actual spine/chest/neck links, never a translated bone root.
  {
    Rig rig;const auto before=rig.poses;float applied=-1;
    const TorsoBones bones{-1,1,2,3,4};
    CHECK(SolveTorsoLean(rig.poses,rig.parents,bones,RootYawPi(),{.1f,0,.05f},applied));
    CHECK(applied>0 && applied<=.112f);
    CHECK(VNear(ArmPoseOrigin(rig.poses[1]),ArmPoseOrigin(before[1])));
    CHECK(VNear(ArmPoseOrigin(rig.poses[11]),ArmPoseOrigin(before[11])));
    const auto lean=Sub(ArmPoseOrigin(rig.poses[3]),ArmPoseOrigin(before[3]));
    CHECK(lean.x<0 && lean.z<0);
    CHECK(std::memcmp(&rig.poses[0],&before[0],sizeof(ArmPoseMatrix))==0);
  }
  {
    Rig rig;float applied=0;
    const TorsoBones bones{-1,1,2,3,4};
    CHECK(SolveTorsoLean(rig.poses,rig.parents,bones,RootYawPi(),{1,0,0},applied));
    CHECK(applied>.15f && applied<=kTorsoLeanLimitM+1e-3f);
    CHECK(VNear(ArmPoseOrigin(rig.poses[1]),ArmPoseOrigin(Rig{}.poses[1])));
  }
  {
    Rig rig;const auto before=rig.poses;float applied=9;
    TorsoBones bones{-1,1,2,3,4};
    CHECK(SolveTorsoLean(rig.poses,rig.parents,bones,RootYawPi(),{},applied));
    CHECK(applied==0);
    ArmPoseMatrix zero{};
    CHECK(!SolveTorsoLean(rig.poses,rig.parents,bones,zero,{.1f,0,0},applied));
    bones.spine=42;
    CHECK(!SolveTorsoLean(rig.poses,rig.parents,bones,RootYawPi(),{.1f,0,0},applied));
    CHECK(std::memcmp(rig.poses.data(),before.data(),sizeof(before))==0);
  }

  // Joint distribution: proportional split, caps, one redistribution.
  {
    const float w[3] = {0.45f, 0.35f, 0.20f};
    const float caps[3] = {0.45f, 0.35f, 0.50f};
    auto a = DistributeJointAngles(0.4f, w, caps);
    CHECK(Near(a[0], 0.18f) && Near(a[1], 0.14f) && Near(a[2], 0.08f));
    auto neg = DistributeJointAngles(-0.4f, w, caps);
    CHECK(Near(neg[0], -0.18f) && Near(neg[1], -0.14f) && Near(neg[2], -0.08f));
    auto big = DistributeJointAngles(2.0f, w, caps);
    CHECK(Near(big[0] + big[1] + big[2], 1.3f, 1e-3f));
    CHECK(big[0] <= caps[0] && big[1] <= caps[1] && big[2] <= caps[2]);
    const float tight[3] = {0.2f, 0.35f, 0.5f};
    auto red = DistributeJointAngles(0.9f, w, tight);
    CHECK(Near(red[0], 0.2f) && Near(red[1], 0.35f));
    CHECK(Near(red[2], 0.2545f, 1e-3f));
    auto zero = DistributeJointAngles(0.0f, w, caps);
    CHECK(zero[0] == 0 && zero[1] == 0 && zero[2] == 0);
  }

  // Torso target: head yaw with a small deadband.
  // Small glances stay head-only; larger turns move the chest 1:1.
    {
      auto turned = [&](float yaw) {
        return VAdd(VScale(kFwd, std::cos(yaw)), VScale(kRight, std::sin(yaw)));
    };
    CHECK(Near(TorsoTargetYaw(turned(0.05f), kRight, kFwd), 0.0f, 1e-3f));
    CHECK(Near(TorsoTargetYaw(turned(-0.05f), kRight, kFwd), 0.0f, 1e-3f));
    // Deadband recenters the response: 0.6 rad in maps to
    // (0.6-0.12)/(0.8-0.12)*0.8 = 0.5647 rad out.
    CHECK(Near(TorsoTargetYaw(turned(0.6f), kRight, kFwd), 0.5647f, 1e-3f));
    CHECK(Near(TorsoTargetYaw(turned(3.0f), kRight, kFwd), 0.8f, 1e-3f));
  }

    // The head alone steers the torso.
    {
    auto turned = [&](float yaw) {
          return VAdd(VScale(kFwd, std::cos(yaw)), VScale(kRight, std::sin(yaw)));
      };
    const Vec3 head = turned(0.3f);
        CHECK(Near(TorsoTargetYaw(head, kRight, kFwd), 0.2118f, 1e-3f));
    }

  // Lean pitch share: deadband, proportion, cap.
  {
    CHECK(TorsoPitchShare(kFwd) == 0.0f);
    CHECK(TorsoPitchShare({0, -std::sin(0.1f), std::cos(0.1f)}) == 0.0f);
    CHECK(Near(TorsoPitchShare({0, -std::sin(0.6f), std::cos(0.6f)}), -0.1575f, 1e-3f));
    CHECK(Near(TorsoPitchShare({0, -1, 0}), -kLeanPitchCapRad, 1e-3f));
    CHECK(Near(TorsoPitchShare({0, std::sin(0.6f), std::cos(0.6f)}), 0.1575f, 1e-3f));
  }

  // Full solver pass on a pelvis->spine->chest->neck->head rig.
  {
    struct Rig2 {
      std::array<ArmPoseMatrix, 13> poses{
          Pose({0, 0, 0}), Pose({0, 0.85f, 0}), Pose({0, 1.0f, 0}),
          Pose({0, 1.2f, 0}), Pose({0, 1.45f, 0}), Pose({0, 1.6f, 0}),
          Pose({-0.2f, 1.35f, 0}), Pose({-0.5f, 1.35f, 0}),
          Pose({-0.75f, 1.35f, 0}), Pose({0.2f, 1.35f, 0}),
          Pose({0.5f, 1.35f, 0}), Pose({0.75f, 1.35f, 0}),
          Pose({0.1f, 0.4f, 0})};
      std::array<int, 13> parents{-1, 0, 1, 2, 3, 4, 3, 6, 7, 3, 9, 10, 1};
    };
    const TorsoBones bones{1, 2, 3, 4, 5};
    const ArmPoseMatrix root = RootYawPi({0, 0, 0});
    auto world = [&](const Rig2& r, int bone) {
      return WorldPoint(root, ArmPoseOrigin(r.poses[bone]));
    };
    auto facing_of = [&](const Rig2& r, int bone) {
      const Vec3 c2 = arm_pose_detail::Column(r.poses[bone], 2);
      return VNorm(Sub({}, WorldPoint(root, c2)));
    };
    // Head turned +0.5 with matching camera: shoulders arrive, pelvis
    // lags at half, head bone faces the camera.
    {
      Rig2 rig;
      const Vec3 leg_before = world(rig, 12), pelvis_before = world(rig, 1);
      const Vec3 cam_f = VAdd(VScale(kFwd, std::cos(0.5f)), VScale(kRight, std::sin(0.5f)));
      const Vec3 cam_r = VAdd(VScale(kRight, std::cos(0.5f)), VScale(kFwd, -std::sin(0.5f)));
      TorsoPoseResult out;
      CHECK(SolveTorsoPose(rig.poses, rig.parents, bones, RootYawPi(),
                           kRight, kFwd, 0.5f, 0.0f, cam_r, kUp, cam_f, out));
      CHECK(Near(out.applied_delta, 0.5f, 1e-3f));
      CHECK(Near(out.pelvis_applied, 0.25f, 1e-2f));
      const float leg_yaw_before = RelativeYaw(VNorm(Sub(leg_before, pelvis_before)), kRight, kFwd);
      const float leg_yaw_after = RelativeYaw(VNorm(Sub(world(rig, 12), world(rig, 1))), kRight, kFwd);
      CHECK(Near(leg_yaw_after - leg_yaw_before, 0.25f, 1e-2f));
      CHECK(VDot(facing_of(rig, 5), cam_f) > 0.999f);
      CHECK(Near(VLen(Sub(world(rig, 8), world(rig, 6))), 0.55f));
    }
    // Head snap beyond its cap aligns only partially.
    {
      Rig2 rig;
      const Vec3 cam_f = VAdd(VScale(kFwd, std::cos(1.2f)), VScale(kRight, std::sin(1.2f)));
      const Vec3 cam_r = VAdd(VScale(kRight, std::cos(1.2f)), VScale(kFwd, -std::sin(1.2f)));
      TorsoPoseResult out;
      CHECK(SolveTorsoPose(rig.poses, rig.parents, bones, RootYawPi(),
                           kRight, kFwd, 0.0f, 0.0f, cam_r, kUp, cam_f, out));
      CHECK(Near(out.snap_applied, kHeadSnapCapRad, 1e-3f));
      CHECK(Near(RelativeYaw(facing_of(rig, 5), kRight, kFwd), kHeadSnapCapRad, 1e-2f));
    }
    // A pitch-only camera difference must not engage the snap: the
    // authored head-pivot offset is unknown, and chasing it saturated
    // the snap at its cap in live testing. Yaw-only alignment instead.
    {
      Rig2 rig;
      const float p = 0.5f;
      const Vec3 cam_f{0, -std::sin(p), std::cos(p)};
      const Vec3 cam_u{0, std::cos(p), std::sin(p)};
      TorsoPoseResult out;
      CHECK(SolveTorsoPose(rig.poses, rig.parents, bones, RootYawPi(),
                           kRight, kFwd, 0.0f, 0.0f, kRight, cam_u, cam_f, out));
      CHECK(out.snap_applied == 0.0f);
      CHECK(Near(RelativeYaw(facing_of(rig, 5), kRight, kFwd), 0.0f, 1e-3f));
    }
    // Downward pitch bends the chest forward (toward facing).
    {
      Rig2 rig;
      const Vec3 chest_before = world(rig, 3);
      TorsoPoseResult out;
      CHECK(SolveTorsoPose(rig.poses, rig.parents, bones, RootYawPi(),
                           kRight, kFwd, 0.0f, -0.1575f, kRight, kUp, kFwd, out));
      const Vec3 chest_after = world(rig, 3);
      CHECK(chest_after.z > chest_before.z + 0.005f);
      CHECK(chest_after.y < chest_before.y);
    }
    // Malformed bones refuse without touching the palette.
    {
      Rig2 rig;
      const auto before = rig.poses;
      TorsoPoseResult out;
      TorsoBones bad = bones; bad.neck = 42;
      CHECK(!SolveTorsoPose(rig.poses, rig.parents, bad, RootYawPi(),
                            kRight, kFwd, 0.5f, 0.0f, kRight, kUp, kFwd, out));
      CHECK(std::memcmp(rig.poses.data(), before.data(), sizeof(rig.poses)) == 0);
    }
  }

  // Damping: snap on prime, exponential approach, deadband, wrap, gap snap.
  {
    DampedAngle d;
    CHECK(Near(d.Approach(1.0f, 1000), 1.0f));
    CHECK(Near(d.Approach(1.0f, 1016), 1.0f));
    CHECK(Near(d.Approach(0.0f, 1106), 0.3679f, 0.01f));
    DampedAngle still;
    CHECK(Near(still.Approach(0.0f, 1000), 0.0f));
    CHECK(Near(still.Approach(0.002f, 1016), 0.0f));
    CHECK(still.Approach(0.01f, 1032) < 0.01f);
    DampedAngle wrap;
    CHECK(Near(wrap.Approach(3.0f, 1000), 3.0f));
    // Short way across the seam: +0.283 rad of travel, wrapped to -3.104.
    CHECK(Near(wrap.Approach(-3.0f, 1090), -3.104f, 0.01f));
    DampedAngle gap;
    CHECK(Near(gap.Approach(0.0f, 1000), 0.0f));
    CHECK(Near(gap.Approach(1.0f, 2000), 1.0f));
    gap.Reset();
    CHECK(Near(gap.Approach(0.25f, 3000), 0.25f));
    DampedVec3 v;
    CHECK(VNear(v.Approach({1, 0, 0}, 1000), {1, 0, 0}));
    CHECK(Near(v.Approach({0, 0, 0}, 1106).x, 0.308f, 0.01f));
  }

  CheckTorsoContracts();
  std::printf("test_body_pose: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
