#include "../src/core/arm_ik_math.h"
#include "../src/core/body_pose_math.h"

#include <array>
#include <cmath>
#include <cstdio>

int main() {
  int failures = 0;
  auto check = [&](bool ok) { if (!ok) ++failures; };
  auto near = [](float a, float b) { return std::fabs(a - b) < 0.002f; };
  auto vnear = [&](dsvr::Vec3 v, float x, float y, float z) {
    return near(v.x, x) && near(v.y, y) && near(v.z, z);
  };
  using namespace dsvr;

  const Vec3 right{1, 0, 0}, up{0, 1, 0}, fwd{0, 0, 1};
  const Vec3 player{0, 0, 0};
  const ArmRig rig;

  // Shoulders sit at height, mirrored by side.
  check(vnear(ShoulderPosition(player, right, up, fwd, 1.0f, rig), 0.21f,
              1.42f, 0.02f));
  check(vnear(ShoulderPosition(player, right, up, fwd, -1.0f, rig), -0.21f,
              1.42f, 0.02f));

  // Elbow poles point down/out, mirrored.
  {
    Vec3 pr = ElbowPoleDirection(right, up, fwd, 1.0f);
    Vec3 pl = ElbowPoleDirection(right, up, fwd, -1.0f);
    check(pr.y < -0.5f && pr.x > 0.1f && pl.x < -0.1f);
    check(near(VLen(pr), 1.0f) && near(VLen(pl), 1.0f));
  }

  // Hand target: hand 0.3 m right of head maps to body-right offset.
  {
    Vec3 head_xr{0, 1.6f, 0}, hand_xr{0.3f, 1.6f, 0};
    Vec3 head_world{0, 1.65f, 0};
    Vec3 t = HandTargetWorld(hand_xr, head_xr, head_world, right, up, fwd,
                             1.0f);
    check(vnear(t, 0.3f, 1.65f, 0.0f));
    // XR -Z (forward) maps to body forward.
    Vec3 hand_fwd{0, 1.6f, -0.4f};
    Vec3 t2 = HandTargetWorld(hand_fwd, head_xr, head_world, right, up, fwd,
                              1.0f);
    check(vnear(t2, 0.0f, 1.65f, 0.4f));
  }

  // Reachable target: wrist lands on target, elbow at link lengths.
  {
    Vec3 shoulder = ShoulderPosition(player, right, up, fwd, 1.0f, rig);
    Vec3 target{shoulder.x + 0.2f, shoulder.y - 0.2f, shoulder.z + 0.2f};
    IkSolution s =
        SolveTwoBoneIk(shoulder, target, ElbowPoleDirection(right, up, fwd, 1.0f), rig);
    check(s.valid && s.reached);
    check(vnear(s.wrist, target.x, target.y, target.z));
    check(near(VLen(Vec3{s.elbow.x - shoulder.x, s.elbow.y - shoulder.y,
                         s.elbow.z - shoulder.z}),
                 rig.upper_length_m));
  }

  // Out-of-reach target: clamped to max reach, reached=false.
  {
    Vec3 shoulder{0, 1.42f, 0};
    Vec3 far{0, 1.42f, 5.0f};
    IkSolution s = SolveTwoBoneIk(shoulder, far, {0, -1, 0}, rig);
    check(s.valid && !s.reached);
    check(near(VLen(Vec3{s.wrist.x - shoulder.x, s.wrist.y - shoulder.y,
                         s.wrist.z - shoulder.z}),
                 rig.upper_length_m + rig.forearm_length_m) ||
          VLen(Vec3{s.wrist.x - shoulder.x, s.wrist.y - shoulder.y,
                    s.wrist.z - shoulder.z}) <=
              rig.upper_length_m + rig.forearm_length_m);
  }

  // Non-finite input fails closed.
  {
    IkSolution s =
        SolveTwoBoneIk({NAN, 0, 0}, {0, 1, 0}, {0, -1, 0}, rig);
    check(!s.valid);
  }

  // QuatFromTo: identity, 90-deg, opposite.
  {
    float q[4];
    QuatFromTo({0, 0, 1}, {0, 0, 1}, q);
    check(near(q[3], 1.0f));
    QuatFromTo({1, 0, 0}, {0, 1, 0}, q);
    Vec3 r = QuatRotate({1, 0, 0}, q[0], q[1], q[2], q[3]);
    check(vnear(r, 0, 1, 0));
    QuatFromTo({0, 0, 1}, {0, 0, -1}, q);
    Vec3 r2 = QuatRotate({0, 0, 1}, q[0], q[1], q[2], q[3]);
    check(vnear(r2, 0, 0, -1));
  }

  // ExpSmooth converges monotonically, invalid dt snaps to target.
  {
    Vec3 c{0, 0, 0}, t{1, 0, 0};
    Vec3 s1 = ExpSmooth(c, t, 1.0f / 90.0f, 18.0f);
    check(s1.x > 0.0f && s1.x < 1.0f);
    Vec3 s2 = ExpSmooth(c, t, -1.0f, 18.0f);
    check(vnear(s2, 1, 0, 0));
  }

  // Continuous root-yaw tracking unwraps the ±pi boundary and a complete turn.
  {
    ContinuousYaw yaw;
    const float pi = 3.14159265358979323846f;
    check(near(yaw.Update(170.0f * pi / 180.0f), 170.0f * pi / 180.0f));
    check(near(yaw.Update(-170.0f * pi / 180.0f), 190.0f * pi / 180.0f));
    yaw.Reset();
    yaw.Update(0.0f);
    yaw.Update(pi * 0.5f);
    yaw.Update(pi);
    yaw.Update(-pi * 0.5f);
    check(near(yaw.Update(0.0f), 2.0f * pi));
  }

  // Leg calibration measures authored thigh/calf lengths; solving a foot target
  // preserves the natural foot basis while using the native bend plane.
  {
    std::array<ArmPoseMatrix, 4> poses{{
        {{1,0,0,0, 0,1,0,0, 0,0,1,0}},
        {{1,0,0,0, 0,1,0,1, 0,0,1,0}},
        {{1,0,0,0, 0,1,0,0.5f, 0,0,1,0}},
        {{1,0,0,0, 0,1,0,0, 0,0,1,0}},
    }};
    const std::array<int, 4> parents{-1, 0, 1, 2};
    const BodyLegBones leg{1, 2, 3};
    BodyLegCalibration calibration{};
    check(CalibrateBodyLeg(poses, parents, leg, calibration));
    check(near(calibration.upper_length, 0.5f) &&
          near(calibration.lower_length, 0.5f));
    const auto solved = SolveBodyLegPose(poses, parents, leg, calibration,
                                         {0.3f, 0.2f, 0.0f});
    check(solved.valid && solved.reached);
    check(vnear(ArmPoseOrigin(poses[3]), 0.3f, 0.2f, 0.0f));
    check(arm_pose_detail::ValidMatrix(poses[3]));
    check(near(VLen(arm_pose_detail::Column(poses[3], 0)), 1.0f));
  }

  // The body-motion estimate comes from the native root, unwraps turns, and
  // rejects teleports; foot targets counter its translation and yaw.
  {
    BodyMotionEstimator motion;
    const float pi = 3.14159265358979323846f;
    check(!motion.Update({0, 0, 0}, 170.0f * pi / 180.0f, 1000).valid);
    const auto step = motion.Update({0.1f, 0, 0}, -170.0f * pi / 180.0f, 1100);
    check(step.valid && near(step.displacement.x, 0.1f) &&
          near(step.velocity.x, 1.0f) && near(step.yaw_delta, 20.0f * pi / 180.0f));
    Vec3 foot_target{};
    check(BodyFootTarget({0.2f, 0, 0}, {0, 0, 0}, step.displacement,
                         pi * 0.5f, foot_target));
    check(vnear(foot_target, -0.1f, 0, 0.2f));
    check(!motion.Update({3.0f, 0, 0}, 0, 1200).valid);
  }

  // The verified FLVER has an anatomical Pelvis sibling to Upper_Root/Spine.
  // The torso solver must support that topology while still applying pelvis yaw.
  {
    std::array<ArmPoseMatrix, 7> poses{{
        {{-1,0,0,0, 0,1,0,0, 0,0,1,0}},
        {{1,0,0,0, 0,1,0,0.7f, 0,0,1,0}},
        {{1,0,0,0, 0,1,0,0.9f, 0,0,1,0}},
        {{1,0,0,0, 0,1,0,1.0f, 0,0,1,0}},
        {{1,0,0,0, 0,1,0,1.25f, 0,0,1,0}},
        {{1,0,0,0, 0,1,0,1.5f, 0,0,1,0}},
        {{1,0,0,0, 0,1,0,1.65f, 0,0,1,0}},
    }};
    const std::array<int, 7> parents{-1, 0, 0, 2, 3, 4, 5};
    const TorsoBones bones{1, 3, 4, 5, 6, 2};
    ArmPoseMatrix root{{-1,0,0,0, 0,1,0,0, 0,0,1,0}};
    TorsoPoseResult result{};
    check(SolveTorsoPose(poses, parents, bones, root, {1,0,0}, {0,0,1},
                         .4f, 0, {-1,0,0}, {0,1,0}, {0,0,-1}, result));
    check(result.valid && std::fabs(result.pelvis_applied) > 0.1f);
  }

  std::printf("arm ik math: %d failures\n", failures);
  return failures ? 1 : 0;
}
