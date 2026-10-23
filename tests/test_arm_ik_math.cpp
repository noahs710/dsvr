#include "../src/core/arm_ik_math.h"
#include "../src/core/body_pose_math.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

int main() {
  int failures = 0;
  auto check_at = [&](bool ok, int line) {
    if (!ok) { ++failures; std::printf("fail near line %d\n", line); }
  };
#define check(ok) check_at((ok), __LINE__)
  auto near = [](float a, float b) { return std::fabs(a - b) < 0.002f; };
  auto vnear = [&](dsvr::Vec3 v, float x, float y, float z) {
    return near(v.x, x) && near(v.y, y) && near(v.z, z);
  };
  using namespace dsvr;

  const Vec3 right{1, 0, 0}, up{0, 1, 0}, fwd{0, 0, 1};
  const Vec3 player{0, 0, 0};
  const ArmRig rig;
  // The conditioning blend must not cancel to a zero bend halfway between
  // opposite native/pole sides. Invalid persistence is not a reference.
  {
    const Vec3 joints[3]{{0,0,0},{.6f,0,.8f},{0,0,1.6f}};
    const float lengths[2]{1,1};
    Vec3 previous{};
    for(float s:{.4999f,.5f,.5001f}) {
      const Vec3 pole{-s,0,std::sqrt(1-s*s)};
      const auto solved=SolveChainIkFabrik(joints,lengths,3,{0,0,1.4f},&pole);
      check(solved.valid && near(VLen(solved.bend_dir),1));
      check(near(VLen(solved.plane_normal),1));
      check(near(VLen(solved.joints[1]),1));
      check(near(VLen(VAdd(solved.joints[2],VScale(solved.joints[1],-1))),1));
      if(VLen(previous)>.9f)check(VDot(previous,solved.bend_dir)>.999f);
      previous=solved.bend_dir;
    }
    const Vec3 pole{-1,0,0};
    ArmBendState stale;stale.bend={0,1,0}; // valid=false deliberately
    const auto clean=SolveChainIkFabrik(joints,lengths,3,{0,0,1.4f},&pole,8,nullptr,4);
    const auto ignored=SolveChainIkFabrik(joints,lengths,3,{0,0,1.4f},&pole,8,&stale,4);
    check(std::memcmp(clean.joints,ignored.joints,sizeof clean.joints)==0);
    const Vec3 bad{NAN,0,0},zero{};
    check(!SolveChainIkFabrik(joints,lengths,3,{0,0,1},&bad).valid);
    check(!SolveChainIkFabrik(joints,lengths,3,{0,0,1},&zero).valid);
    check(!SolveChainIkFabrik(joints,lengths,3,{0,0,1},&pole,8,nullptr,NAN).valid);
  }
    // Opposite poles have no interpolation tangent; the budget must still hold.
    for(float y:{0.f,1e-7f,-1e-7f}) {
      const Vec3 joints[3]{{0,0,0},{.6f,0,.8f},{0,0,1.6f}};
      const float lengths[2]{1,1};
      const Vec3 target{0,0,1.4f},pole{-1,y,0},reference{1,0,0};
      const auto limited=SolveChainIkFabrik(joints,lengths,3,target,&pole,8,nullptr,4.f);
      check(limited.valid && limited.reached);
      const float step=std::acos(std::clamp(VDot(reference,limited.bend_dir),-1.f,1.f))*180.f/3.14159265f;
      check(std::fabs(step-4.f)<.01f);
      check(near(VLen(VAdd(limited.joints[1],VScale(limited.joints[0],-1))),1.f));
      check(near(VLen(VAdd(limited.joints[2],VScale(limited.joints[1],-1))),1.f));
      const auto again=SolveChainIkFabrik(joints,lengths,3,target,&pole,8,nullptr,4.f);
      check(std::memcmp(limited.joints,again.joints,sizeof limited.joints)==0);
      const auto raw=SolveChainIkFabrik(joints,lengths,3,target,&pole);
      const auto unbounded=SolveChainIkFabrik(joints,lengths,3,target,&pole,8,nullptr,180.f);
      check(std::memcmp(raw.joints,unbounded.joints,sizeof raw.joints)==0);
    }

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

  // FABRIK chain core (2026-10-22 production decision, bend-plane rate budget
  // 2026-10-22): with the pole well conditioned about the achieved chord
  // (perp magnitude >= 0.7) the three-joint chain must equal the retained
  // two-bone reference exactly -- the pole is honored as a preference there,
  // so the arm_pose_math.h transport and naturalness fixtures carry over. In
  // degraded conditioning equivalence with the reference is NOT required (the
  // reference is what whipped the elbow); assert the invariants instead:
  // identical clamped wrist, exact link lengths, unit bend/plane, determinism.
  {
    const Vec3 shoulder{0.f, 1.42f, 0.f};
    const float l0 = rig.upper_length_m, l1 = rig.forearm_length_m;
    const float chain_links[2] = {l0, l1};
    const Vec3 poles[4] = {ElbowPoleDirection(right, up, fwd, 1.0f),
                           {0, -1, 0}, {0, 1, 0}, {0, 0, 1}};
    float max_component_diff = 0.f;
    int conditioned = 0, degraded = 0;
    for (const Vec3& pole : poles) {
      const Vec3 pole_unit = VNorm(pole);
      for (int i = 0; i < 24; ++i) {
        const float ang = i * 0.261799f;  // 15 deg steps
        const float radius = 0.01f + 0.055f * i;  // fold, reach, saturation
        Vec3 target{shoulder.x + radius * std::cos(ang),
                    shoulder.y + radius * std::sin(ang) * 0.6f,
                    shoulder.z + radius * std::cos(ang * 0.5f)};
        const Vec3 chain_joints[3] = {shoulder, {shoulder.x, shoulder.y - l0, shoulder.z},
                                      {shoulder.x, shoulder.y - l0 - l1, shoulder.z}};
        const IkSolution ref = SolveTwoBoneIk(shoulder, target, pole, rig);
        const IkSolution fab = IkSolutionFromChain(SolveChainIkFabrik(
            chain_joints, chain_links, 3, target, &pole));
        check(ref.valid == fab.valid);
        if (!ref.valid || !fab.valid) continue;
        check(ref.reached == fab.reached);
        // Conditioning of this pole about the chord the solve achieved.
        const Vec3 achieved{fab.wrist.x - shoulder.x, fab.wrist.y - shoulder.y,
                            fab.wrist.z - shoulder.z};
        const float ach = VLen(achieved);
        const Vec3 chord_dir = ach > 1e-6f ? VScale(achieved, 1.f / ach)
                                           : Vec3{0, 0, 0};
        const Vec3 pole_perp{
            pole_unit.x - chord_dir.x * VDot(pole_unit, chord_dir),
            pole_unit.y - chord_dir.y * VDot(pole_unit, chord_dir),
            pole_unit.z - chord_dir.z * VDot(pole_unit, chord_dir)};
        const float s_p = VLen(pole_perp);
        // The clamped wrist and the two link lengths are blend-independent.
        const Vec3 wd{fab.wrist.x - ref.wrist.x, fab.wrist.y - ref.wrist.y,
                      fab.wrist.z - ref.wrist.z};
        max_component_diff =
            max_component_diff > VLen(wd) ? max_component_diff : VLen(wd);
        check(near(VLen(Vec3{fab.elbow.x - shoulder.x, fab.elbow.y - shoulder.y,
                             fab.elbow.z - shoulder.z}), l0));
        check(near(VLen(Vec3{fab.wrist.x - fab.elbow.x,
                             fab.wrist.y - fab.elbow.y,
                             fab.wrist.z - fab.elbow.z}), l1));
        check(near(VLen(fab.bend_dir), 1.f));
        check(near(VLen(fab.plane_normal), 1.f));
        if (s_p >= 0.7f) {  // full-pole band: exact reference equivalence
          ++conditioned;
          const Vec3 diffs[3] = {fab.elbow, fab.bend_dir, fab.plane_normal};
          const Vec3 refs[3] = {ref.elbow, ref.bend_dir, ref.plane_normal};
          for (int k = 0; k < 3; ++k) {
            const Vec3 d{diffs[k].x - refs[k].x, diffs[k].y - refs[k].y,
                         diffs[k].z - refs[k].z};
            max_component_diff =
                max_component_diff > VLen(d) ? max_component_diff : VLen(d);
          }
        } else {
          ++degraded;
          const auto again = IkSolutionFromChain(SolveChainIkFabrik(
              chain_joints, chain_links, 3, target, &pole));
          check(again.elbow.x == fab.elbow.x && again.elbow.y == fab.elbow.y &&
                again.elbow.z == fab.elbow.z && again.wrist.x == fab.wrist.x &&
                again.wrist.y == fab.wrist.y && again.wrist.z == fab.wrist.z);
        }
      }
    }
    check(conditioned >= 40 && degraded >= 10);  // grid spans both bands
    check(max_component_diff < 0.001f);

    // Degenerate-conditioning sweep with a BENT chain: pole straight down,
    // targets straight down (s_p ~ 0 about the achieved chord), native elbow
    // offset lateral. The bend must stay on the native bend side -- exactly
    // the handover that keeps a near-parallel pole from flipping the elbow.
    {
      const Vec3 pole{0, -1, 0};
      const Vec3 bs{0.f, 1.42f, 0.f};
      const Vec3 be{bs.x + 0.20f, bs.y - 0.20f, bs.z};
      const Vec3 bw{be.x + 0.10f, be.y - 0.28f, bs.z - 0.02f};
      const Vec3 chain_joints[3] = {bs, be, bw};
      const Vec3 off{be.x - bs.x, be.y - bs.y, be.z - bs.z};
      int exercised = 0;
      for (float r = 0.05f; r <= 0.66f; r += 0.05f) {
        const Vec3 target{bs.x, bs.y - r, bs.z};
        const IkSolution fab = IkSolutionFromChain(SolveChainIkFabrik(
            chain_joints, chain_links, 3, target, &pole));
        check(fab.valid);
        if (!fab.valid) continue;
        ++exercised;
        const Vec3 a{fab.wrist.x - bs.x, fab.wrist.y - bs.y, fab.wrist.z - bs.z};
        const float ach = VLen(a);
        const Vec3 dir = ach > 1e-6f ? VScale(a, 1.f / ach) : Vec3{0, -1, 0};
        const float cp = VDot(pole, dir);
        const float s_p = VLen(Vec3{pole.x - dir.x * cp, pole.y - dir.y * cp,
                                    pole.z - dir.z * cp});
        check(s_p < 0.3f);  // fully degraded band by construction
        const Vec3 nperp{off.x - dir.x * VDot(off, dir),
                         off.y - dir.y * VDot(off, dir),
                         off.z - dir.z * VDot(off, dir)};
        check(VLen(nperp) > 0.05f);
        check(VDot(fab.bend_dir, VNorm(nperp)) > 0.9f);  // native side held
        check(near(VLen(Vec3{fab.elbow.x - bs.x, fab.elbow.y - bs.y,
                             fab.elbow.z - bs.z}), l0));
        check(near(VLen(Vec3{fab.wrist.x - fab.elbow.x, fab.wrist.y - fab.elbow.y,
                             fab.wrist.z - fab.elbow.z}), l1));
        check(near(VLen(fab.bend_dir), 1.f));
        const auto again = IkSolutionFromChain(SolveChainIkFabrik(
            chain_joints, chain_links, 3, target, &pole));
        check(again.elbow.x == fab.elbow.x && again.elbow.y == fab.elbow.y &&
              again.elbow.z == fab.elbow.z);
      }
      check(exercised >= 10);
    }

    // Bend-plane rate budget: a conditioned pole that disagrees with the
    // entering chain's bend side by far more than the budget must not
    // teleport the elbow across the chord in one solve (the arm_ik_naturalness
    // cross-body f87-90 whip: agree(pu, nu) crossing zero with the pole
    // cot-swinging drove the UpperArm transport plane 68.8 deg in a frame).
    // The bend walks the nu->pu arc by at most the budget; a budget above the
    // arc equals the unbudgeted blend exactly, so the limiter only rate-limits.
    {
      const Vec3 bs{0.f, 1.42f, 0.f};
      const Vec3 be{bs.x + 0.20f, bs.y - 0.20f, bs.z};
      const Vec3 bw{be.x + 0.10f, be.y - 0.28f, bs.z - 0.02f};
      const Vec3 chain_joints[3] = {bs, be, bw};
      const Vec3 pole{-0.5f, -0.866f, 0.5f};  // conditioned, bend side opposite nu
      const Vec3 target{bs.x, bs.y - 0.5f, bs.z};
      const auto raw = IkSolutionFromChain(
          SolveChainIkFabrik(chain_joints, chain_links, 3, target, &pole));
      const auto limited = IkSolutionFromChain(SolveChainIkFabrik(
          chain_joints, chain_links, 3, target, &pole, 8, nullptr, 6.f));
      const auto swept = IkSolutionFromChain(SolveChainIkFabrik(
          chain_joints, chain_links, 3, target, &pole, 8, nullptr, 180.f));
      check(raw.valid && limited.valid && swept.valid);
      const Vec3 ach{limited.wrist.x - bs.x, limited.wrist.y - bs.y,
                     limited.wrist.z - bs.z};
      const float ach_l = VLen(ach);
      const Vec3 dir = ach_l > 1e-6f ? VScale(ach, 1.f / ach_l) : Vec3{0, -1, 0};
      const Vec3 off{be.x - bs.x, be.y - bs.y, be.z - bs.z};
      const Vec3 nperp{off.x - dir.x * VDot(off, dir),
                       off.y - dir.y * VDot(off, dir),
                       off.z - dir.z * VDot(off, dir)};
      check(VLen(nperp) > 0.05f);
      const Vec3 nu = VNorm(nperp);
      const auto angle_from_nu = [&](const IkSolution& s) {
        return std::acos(std::clamp(VDot(s.bend_dir, nu), -1.f, 1.f)) /
               0.017453292519943295f;
      };
      check(angle_from_nu(raw) > 60.f);    // real disagreement to rate-limit
      check(angle_from_nu(limited) <= 6.01f);  // walk bounded per solve
      check(angle_from_nu(limited) >= 5.99f);  // ...but still walks
      check(std::memcmp(&swept.bend_dir, &raw.bend_dir, sizeof(Vec3)) == 0 &&
            swept.elbow.x == raw.elbow.x && swept.elbow.y == raw.elbow.y &&
            swept.elbow.z == raw.elbow.z);  // budget above the arc: endpoint
    }
  }

  // A four-joint chain (clavicle-style) reaches its target, keeps every link
  // exact, never moves the root, is deterministic, and fails closed.
  {
    const Vec3 chain_joints[4] = {{0, 0, 0}, {0.5f, 0, 0}, {1.0f, 0, 0}, {1.5f, 0, 0}};
    const float chain_links[3] = {0.5f, 0.5f, 0.5f};
    const Vec3 target{0.3f, 0.9f, 0.f};
    const auto solved = SolveChainIkFabrik(chain_joints, chain_links, 4, target,
                                           nullptr, 40);
    check(solved.valid && solved.reached && solved.joint_count == 4);
    const Vec3& end = solved.joints[3];
    check(VLen(Vec3{end.x - target.x, end.y - target.y, end.z - target.z}) <
          1e-3f);
    for (int i = 0; i < 3; ++i) {
      const Vec3 v{solved.joints[i + 1].x - solved.joints[i].x,
                   solved.joints[i + 1].y - solved.joints[i].y,
                   solved.joints[i + 1].z - solved.joints[i].z};
      check(near(VLen(v), chain_links[i]));
    }
    check(vnear(solved.joints[0], 0, 0, 0));
    const auto again = SolveChainIkFabrik(chain_joints, chain_links, 4, target,
                                          nullptr, 40);
    check(std::memcmp(again.joints, solved.joints, sizeof solved.joints) == 0);
    // Unreachable: saturates straight rather than failing.
    const auto saturated = SolveChainIkFabrik(chain_joints, chain_links, 4,
                                              {9.f, 0, 0});
    check(saturated.valid && !saturated.reached &&
          near(VLen(saturated.joints[3]), 1.5f));
    const Vec3 bad[2] = {{NAN, 0, 0}, {1, 0, 0}};
    const float links[1] = {0.5f};
    check(!SolveChainIkFabrik(bad, links, 2, {1, 1, 1}).valid);
    check(!SolveChainIkFabrik(chain_joints, chain_links, 1, target).valid);
    const float zero_len[1] = {0.f};
    check(!SolveChainIkFabrik(chain_joints, zero_len, 2, target).valid);
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
