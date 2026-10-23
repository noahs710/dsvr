#pragma once
#include <algorithm>
#include <cmath>

#include "camera_math.h"

namespace dsvr {

// Pure, unit-tested IK for driving the *actual player model's* arms from
// OpenXR hand poses (approved design 2026-10-13). Since the 2026-10-22 plan
// decision the production reach solver is the FABRIK chain core below;
// SolveTwoBoneIk stays as its characterization reference under unit test,
// not a second production path. Runtime writes in
// arm_ik.cpp require the verified PlayerIns vtable seam, a live FLVER rig, and
// the post-writer output/palette alias check. Everything testable without a
// headset -- reach clamping, elbow placement, XR mapping, and smoothing -- is
// decided here; live body quality remains separately unverified.
//
// Conventions:
// - World/player space uses the game basis (right/up/forward rows,
//   forward = character facing, +Y world up), metres.
// - XR LOCAL space: +X right, +Y up, -Z forward (OpenXR). The hand
//   target is head-relative so arm reach is measured from the head,
//   exactly as a real arm is measured from the shoulder near it.
// - Quaternions are xyzw. Bone aiming is expressed as a from->to
//   rotation so arm_ik.cpp can compose it with each bone's rest pose
//   once the skeleton convention is verified -- no bone-axis guess is
//   baked in here.

struct ArmRig {
  float shoulder_height_m = 1.42f;  // above coherent character origin
  float shoulder_width_m = 0.21f;   // lateral offset from body centre
  float shoulder_forward_m = 0.02f;
  float upper_length_m = 0.28f;     // shoulder -> elbow
  float forearm_length_m = 0.30f;   // elbow -> wrist
};

struct IkSolution {
  Vec3 shoulder{0, 0, 0};
  Vec3 elbow{0, 0, 0};
  Vec3 wrist{0, 0, 0};   // reach-clamped target actually achieved
  Vec3 upper_dir{0, 0, 1};
  Vec3 forearm_dir{0, 0, 1};
  // Elbow hinge side: the pole direction with the shoulder->wrist line removed
  // and normalized, i.e. the direction the elbow offset lies in. It stays unit
  // length through full extension, unlike the perpendicular component of the
  // solved forearm, which collapses exactly when the arm straightens.
  Vec3 bend_dir{0, 0, 0};
  // Normal of the plane the solve lives in (shoulder->wrist line x bend_dir).
  // Continuous along a moving target, so callers can decompose a shoulder
  // swing into an in-plane swing and an out-of-plane roll without ever hitting
  // the half-turn degeneracy of a shortest-arc axis.
  Vec3 plane_normal{0, 0, 0};
  bool reached = false;  // raw target lies inside the reachable annulus
  bool valid = false;
};

inline bool FiniteVec(Vec3 v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// Shoulder anchor on the body: character origin + basis offsets. `side`
// is +1 for the body's right, -1 for its left, in the body-right axis.
inline Vec3 ShoulderPosition(Vec3 player_pos, Vec3 body_right, Vec3 body_up,
                             Vec3 body_fwd, float side,
                             const ArmRig& rig = {}) {
  return VAdd(VAdd(VAdd(player_pos, VScale(body_up, rig.shoulder_height_m)),
                   VScale(body_right, side * rig.shoulder_width_m)),
              VScale(body_fwd, rig.shoulder_forward_m));
}

// Elbow pole: down, slightly back, and outward, so elbows point where
// human elbows point when holding controllers (never into the torso).
inline Vec3 ElbowPoleDirection(Vec3 body_right, Vec3 body_up, Vec3 body_fwd,
                               float side) {
  Vec3 p = VAdd(VAdd(VScale(body_up, -1.0f), VScale(body_fwd, -0.35f)),
                VScale(body_right, side * 0.45f));
  return VLen(p) > 1e-6f ? VNorm(p) : Vec3{0, -1, 0};
}

// XR hand target in world space: head world anchor plus the hand's
// offset from the head, expressed in the body basis (reuses the
// camera's HeadTranslationWorld mapping: XR -Z maps to body forward).
inline Vec3 HandTargetWorld(Vec3 hand_xr, Vec3 head_xr, Vec3 head_world,
                            Vec3 body_right, Vec3 body_up, Vec3 body_fwd,
                            float world_scale) {
  if (!FiniteVec(hand_xr) || !FiniteVec(head_xr) || !FiniteVec(head_world))
    return head_world;
  Vec3 delta{hand_xr.x - head_xr.x, hand_xr.y - head_xr.y,
             hand_xr.z - head_xr.z};
  return VAdd(head_world, HeadTranslationWorld(delta, body_right, body_up,
                                               body_fwd, world_scale));
}

// Classic two-bone IK by the law of cosines. The wrist is clamped onto
// the reachable sphere; `pole_dir` picks the elbow bend plane. Fails
// closed (valid=false) on non-finite input or degenerate lengths.
inline IkSolution SolveTwoBoneIk(Vec3 shoulder, Vec3 target, Vec3 pole_dir,
                                 const ArmRig& rig = {}) {
  IkSolution out;
  out.shoulder = shoulder;
  if (!FiniteVec(shoulder) || !FiniteVec(target) || !FiniteVec(pole_dir) ||
      !std::isfinite(rig.upper_length_m) ||
      !std::isfinite(rig.forearm_length_m) ||
      !(rig.upper_length_m > 1e-4f) || !(rig.forearm_length_m > 1e-4f))
    return out;
  const float l1 = rig.upper_length_m, l2 = rig.forearm_length_m;
  Vec3 to{target.x - shoulder.x, target.y - shoulder.y,
          target.z - shoulder.z};
  float dist = VLen(to);
  const float max_reach = l1 + l2;
  const float min_reach = std::fabs(l1 - l2);
  if (!std::isfinite(dist) || !std::isfinite(max_reach)) return out;
  out.reached = dist >= min_reach && dist <= max_reach;
  float clamped = dist;
  if (clamped > max_reach) clamped = max_reach;
  // Equal lengths can fold onto the shoulder. Keep the law-of-cosines
  // denominator nonzero without stretching either link.
  const float solve_min = min_reach > 1e-6f ? min_reach : 1e-6f;
  if (clamped < solve_min) clamped = solve_min;
  Vec3 dir = dist > 1e-6f ? VScale(to, 1.0f / dist) : Vec3{0, 0, 1};
  Vec3 wrist = VAdd(shoulder, VScale(dir, clamped));
  float a = (l1 * l1 - l2 * l2 + clamped * clamped) / (2.0f * clamped);
  float h2 = l1 * l1 - a * a;
  float h = h2 > 0.0f ? std::sqrt(h2) : 0.0f;
  Vec3 bend = VAdd(pole_dir, VScale(dir, -VDot(pole_dir, dir)));
  if (VLen(bend) < 1e-5f) {  // pole parallel to arm: pick any perpendicular
    Vec3 arb = std::fabs(dir.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    bend = VAdd(arb, VScale(dir, -VDot(arb, dir)));
  }
  bend = VNorm(bend);
  out.bend_dir = bend;
  out.plane_normal = VNorm(VCross(dir, bend));
  out.elbow = VAdd(VAdd(shoulder, VScale(dir, a)), VScale(bend, h));
  out.wrist = wrist;
  out.upper_dir = VNorm(Vec3{out.elbow.x - shoulder.x,
                             out.elbow.y - shoulder.y,
                             out.elbow.z - shoulder.z});
  out.forearm_dir = VNorm(Vec3{wrist.x - out.elbow.x, wrist.y - out.elbow.y,
                               wrist.z - out.elbow.z});
  out.valid = FiniteVec(out.elbow) && FiniteVec(out.wrist) &&
              FiniteVec(out.upper_dir) && FiniteVec(out.forearm_dir) &&
              FiniteVec(out.bend_dir) && FiniteVec(out.plane_normal);
  return out;
}

// ---------------------------------------------------------------------------
// FABRIK chain core (Andreason & Baath 2011), production per 2026-10-22.
// Forward-and-backward reaching over joint positions with bounded passes,
// initialized from the caller's current chain. Consumers (arm_pose_math.h)
// transport the placement into the palette as rigid subtree deltas, so
// scale, exact link lengths and authored twist relationships hold by
// construction. With three joints and a pole, the backswing places the
// middle joint in the pole plane exactly where the law of cosines puts it
// while the pole is well conditioned about the solved chord (unit-tested
// against the retained reference); in degraded conditioning it blends toward
// the chain's native bend side instead of letting a near-parallel pole whip
// the elbow. Chains of four or more joints (clavicle, legs) omit the pole and
// bend from the current pose; authored per-joint axis clamps belong at the
// transport layer, not here.
struct ChainIkSolution {
  static constexpr int kMaxJoints = 8;
  Vec3 joints[kMaxJoints]{};  // valid over [0, joint_count)
  int joint_count = 0;
  // Three-joint chains only: unit middle-joint offset direction and solve
  // plane normal, both continuous along a moving target and stable through
  // extension, mirroring the IkSolution contract.
  Vec3 bend_dir{0, 0, 0};
  Vec3 plane_normal{0, 0, 0};
  bool reached = false;  // raw target lies inside the reachable ball
  bool valid = false;
};

// joints: current chain, root first; lengths: joint_count-1 links. The root
// never moves. Unreachable targets saturate to a straightened chain toward
// them instead of failing. Malformed input fails closed (valid=false).
// Deterministic: identical inputs produce identical outputs.
//
// Per-chain rate persistence for three-joint pole solves lives in
// ArmBendState. The production solve reseeds the chain from the live palette
// every frame, so the solver cannot see the previous solve's bend plane on
// its own; callers keep one of these per arm, pass it in, and store the
// returned bend_dir (plus the exact inputs it was solved from) back into it.
// Reset it at any pose discontinuity (teleport guard, animation revision
// change, stale frame gap) so the budget never chases a reference the pose
// itself already jumped past.
struct ArmBendState {
  Vec3 bend{};
  // Exact solver inputs that produced `bend`. A re-processed identical input
  // (steady animation, duplicate capture frame) must return the stored bend
  // bit-identically, not spend one more budget step; only genuinely new
  // inputs walk at budget rate.
  Vec3 joints[3]{};
  float lengths[2]{};
  Vec3 target{};
  Vec3 pole{};  // normalized, exactly as the solver uses it
  bool valid = false;
  void Reset() { *this = ArmBendState{}; }
};

// max_bend_step_deg (optional, 0 = off): per-solve rotation budget for the
// three-joint bend direction about the solved chord. The pole blend below
// hands the bend over from the chain's own side to the pole as conditioning
// degrades; per-frame callers pass this to bound the handover's angular rate.
// prev_state (optional): the ArmBendState carried across solves of this same
// chain. The budget is spent against its stored bend; when the current
// inputs match its stored inputs bit-exactly, the stored bend is returned
// unchanged instead (a re-solve of an unchanged input never walks). Without
// it the incoming chain's bend side is the reference.
// forced_bend (optional): pin the bend plane to a caller-chosen direction
// instead of deriving it from the pole blend. SolveModelArmPoseWithClavicle
// uses this so a frame decides its plane once (the solve that spent the rate
// budget) and the re-solve from the clavicle-shifted shoulder keeps it; the
// direction is re-perpendicularized to the current chord, so only the plane
// is inherited, not the elbow position.
inline ChainIkSolution SolveChainIkFabrik(const Vec3* joints,
                                          const float* lengths,
                                          int joint_count, Vec3 target,
                                          const Vec3* pole_dir = nullptr,
                                          int max_passes = 8,
                                          const ArmBendState* prev_state = nullptr,
                                          float max_bend_step_deg = 0.f,
                                          const Vec3* forced_bend = nullptr) {
  ChainIkSolution out;
  const int n = joint_count;
  if (!joints || !lengths || n < 2 || n > ChainIkSolution::kMaxJoints ||
      !FiniteVec(target) || !(max_passes > 0) ||
      !std::isfinite(max_bend_step_deg) || max_bend_step_deg < 0.f ||
      (pole_dir && (!FiniteVec(*pole_dir) || !std::isfinite(VLen(*pole_dir)) ||
                    VLen(*pole_dir) < 1e-5f)) ||
      (forced_bend && (!FiniteVec(*forced_bend) || !std::isfinite(VLen(*forced_bend)) ||
                      VLen(*forced_bend) < 1e-5f)))
    return out;
  Vec3 x[ChainIkSolution::kMaxJoints]{};
  for (int i = 0; i < n; ++i) {
    x[i] = joints[i];
    if (!FiniteVec(x[i])) return out;
  }
  float len[ChainIkSolution::kMaxJoints - 1]{};
  float total = 0.f;
  for (int i = 0; i < n - 1; ++i) {
    len[i] = lengths[i];
    if (!std::isfinite(len[i]) || !(len[i] > 1e-4f)) return out;
    total += len[i];
  }
  const Vec3 root = x[0];
  const Vec3 to_target{target.x - root.x, target.y - root.y,
                       target.z - root.z};
  const float dist = VLen(to_target);
  if (!std::isfinite(dist) || !std::isfinite(total) || !(total > 0.f)) return out;
  float longest = len[0];
  for (int i = 1; i < n - 1; ++i)
    if (len[i] > longest) longest = len[i];
  const float min_annulus = longest > total - longest ? longest - (total - longest) : 0.f;
  out.reached = dist >= min_annulus && dist <= total;
  Vec3 bend_dir{0, 0, 0}, plane_normal{0, 0, 0};
  const bool pole_active = n == 3 && pole_dir && FiniteVec(*pole_dir);
  // Native bend side of the caller's current chain: the middle joint offset
  // perpendicular to the chord. The pole is a preference only while well
  // conditioned about the solved chord. As the pole approaches the chord
  // (cross-body/overhead passages), its perpendicular component shrinks and
  // the direction it points rotates ever faster about the chord (cot swing),
  // whipping the elbow offset tens of centimetres per frame. Outside that
  // band the backswing hands the bend over to the chain's own bend side,
  // FABRIK's natural chord-plane solution from the current pose, which moves
  // with the pose. Stateless and deterministic per frame; blend band
  // s_p in [0.3, 0.7] of perpendicular magnitude.
  const Vec3 init_mid = x[1];
  constexpr float kRad = 0.017453292519943295f;
  auto apply_pole = [&]() {
    const Vec3 pole = VNorm(*pole_dir);
    auto bits = [](const Vec3& a, const Vec3& b) {
      return a.x == b.x && a.y == b.y && a.z == b.z;
    };
    // Re-processed identical inputs (steady animation, duplicate capture
    // frame) return the stored bend bit-exactly; only new inputs walk.
    const bool steady_input =
        max_bend_step_deg > 0.f && prev_state && prev_state->valid &&
        FiniteVec(prev_state->bend) && VLen(prev_state->bend) > .99f &&
        prev_state->lengths[0] == len[0] && prev_state->lengths[1] == len[1] &&
        bits(joints[0], prev_state->joints[0]) &&
        bits(joints[1], prev_state->joints[1]) &&
        bits(joints[2], prev_state->joints[2]) &&
        bits(target, prev_state->target) && bits(pole, prev_state->pole);
    const float l0 = len[0], l1 = len[1];
    Vec3 chord{x[2].x - x[0].x, x[2].y - x[0].y, x[2].z - x[0].z};
    float d = VLen(chord);
    Vec3 dir{0, 0, 1};
    if (d > 1e-6f)
      dir = VScale(chord, 1.f / d);
    else {  // chord collapsed: keep the current upper-arm direction
      const Vec3 cur{x[1].x - x[0].x, x[1].y - x[0].y, x[1].z - x[0].z};
      if (VLen(cur) > 1e-6f) dir = VNorm(cur);
    }
    const float min_reach = std::fabs(l0 - l1);
    const float solve_min = min_reach > 1e-6f ? min_reach : 1e-6f;
    if (!(d >= solve_min)) {  // fold clamp, same as the reference solve
      d = solve_min;
      x[2] = VAdd(x[0], VScale(dir, d));
    }
    const float a = (l0 * l0 - l1 * l1 + d * d) / (2.f * d);
    const float h2 = l0 * l0 - a * a;
    const float h = h2 > 0.f ? std::sqrt(h2) : 0.f;
    const Vec3 pole_perp{pole.x - dir.x * VDot(pole, dir),
                         pole.y - dir.y * VDot(pole, dir),
                         pole.z - dir.z * VDot(pole, dir)};
    const float s_p = VLen(pole_perp);
    const Vec3 init_off{init_mid.x - x[0].x, init_mid.y - x[0].y,
                        init_mid.z - x[0].z};
    const Vec3 native_perp{
        init_off.x - dir.x * VDot(init_off, dir),
        init_off.y - dir.y * VDot(init_off, dir),
        init_off.z - dir.z * VDot(init_off, dir)};
    const float s_c = VLen(native_perp);
    const float w_cond =
        s_p <= 0.3f ? 0.f : (s_p >= 0.7f ? 1.f : (s_p - 0.3f) / 0.4f);
    const float w = w_cond;
    Vec3 bend{0, 0, 0};
    if (s_p > 1e-5f && s_c > 1e-5f) {
      // Conditioning-only blend: at s_p >= 0.7 the pole wins outright, so the
      // solve matches the retained reference exactly there even when native
      // animation bends the elbow across the pole side. s_p is continuous and
      // the blend is monotone in it, so the handover itself never flips the
      // elbow side discontinuously.
      const Vec3 pu{pole_perp.x / s_p, pole_perp.y / s_p, pole_perp.z / s_p};
      const Vec3 nu{native_perp.x / s_c, native_perp.y / s_c,
                    native_perp.z / s_c};
      if (VDot(pu,nu) < -.99999f && w > 0.f && w < 1.f) {
        // Opposite sides cancel under a linear blend. Follow a deterministic
        // half-circle in the chord plane instead: unit bend, exact links,
        // and no zero-vector cusp at the middle of the conditioning band.
        const float angle=3.14159265358979323846f*w;
        bend=VAdd(VScale(nu,std::cos(angle)),
                  VScale(VCross(dir,nu),std::sin(angle)));
      } else bend = VAdd(VScale(pu, w), VScale(nu, 1.f - w));
    } else if (s_p > 1e-5f) {
      bend = pole_perp;
    } else if (s_c > 1e-5f) {
      bend = native_perp;
    } else {  // both degenerate: any perpendicular
      const Vec3 arb = std::fabs(dir.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
      bend = {arb.x - dir.x * VDot(arb, dir), arb.y - dir.y * VDot(arb, dir),
              arb.z - dir.z * VDot(arb, dir)};
    }
    bend = VNorm(bend);
    // Bend-plane rate budget (per solve). The blend answer above is
    // endpoint-correct but not per-frame safe: as s_p collapses the pole's
    // perpendicular cot-swings about the chord, and once the blend crosses
    // zero agreement with the chain side the achieved bend direction rotates
    // tens of degrees in one frame -- the transport turns the UpperArm plane
    // with it (arm_ik_naturalness cross-body f87-90: 68.8 deg UpperArm basis
    // delta where the authored bound is 12). Rate-limiting the blend WEIGHT
    // cannot bound this; the whipping quantity is the final direction's
    // rotation about the chord, not the weight. So cap the bend's angular
    // STEP toward the blend answer instead: the reference is the previous
    // solve's bend (prev_bend, caller-persistent because the production chain
    // is reseeded from the animation palette each frame), falling back to the
    // incoming chain's own bend side; the step toward the blend answer is at
    // most max_bend_step_deg. Endpoints unchanged: with the plane already at
    // the blend answer nothing moves, and budget 0 (the unit-tested default)
    // reproduces the stateless blend exactly.
    if (max_bend_step_deg > 0.f && !steady_input) {
      Vec3 ref{0, 0, 0};
      bool have_ref = false;
      if (prev_state && prev_state->valid && FiniteVec(prev_state->bend)) {
        const float pd = VDot(prev_state->bend, dir);
        const Vec3 p{prev_state->bend.x - dir.x * pd,
                     prev_state->bend.y - dir.y * pd,
                     prev_state->bend.z - dir.z * pd};
        if (VLen(p) > 1e-4f) { ref = VNorm(p); have_ref = true; }
      }
      if (!have_ref && s_c > 1e-5f) {
        ref = VScale(native_perp, 1.f / s_c);
        have_ref = true;
      }
      if (have_ref) {
        const float cd = std::clamp(VDot(bend, ref), -1.f, 1.f);
        const float budget = max_bend_step_deg * kRad;
        if (std::acos(cd) > budget) {
          Vec3 t{bend.x - ref.x * cd, bend.y - ref.y * cd,
                 bend.z - ref.z * cd};
          float tl = VLen(t);
          // Antipodal bends have no interpolation tangent. Choose one in the
          // chord plane deterministically, rather than skipping the cap or
          // letting sub-ulp pole noise choose a 180-degree elbow flip.
          if (tl <= 1e-6f) { t = VCross(dir, ref); tl = VLen(t); }
          if (tl > 1e-6f) {
            const float inv = 1.f / tl;
            bend = VAdd(VScale(ref, std::cos(budget)),
                        VScale(Vec3{t.x * inv, t.y * inv, t.z * inv},
                               std::sin(budget)));
          }
        }
      }
    }
    if (steady_input) bend = prev_state->bend;  // bit-exact reuse
    if (forced_bend && FiniteVec(*forced_bend)) {
      // Inherited plane, current chord: only the component of the pinned bend
      // perpendicular to this chord survives, so the elbow still lands on the
      // arc the new shoulder->wrist distance allows.
      const float fd = VDot(*forced_bend, dir);
      const Vec3 f{forced_bend->x - dir.x * fd, forced_bend->y - dir.y * fd,
                   forced_bend->z - dir.z * fd};
      if (VLen(f) > 1e-4f) bend = VNorm(f);
    }
    x[1] = VAdd(VAdd(x[0], VScale(dir, a)), VScale(bend, h));
    bend_dir = bend;
    plane_normal = VNorm(VCross(dir, bend));
  };
  Vec3 prev[ChainIkSolution::kMaxJoints]{};
  if (dist > total) {  // warm start for an unreachable target: straighten
    const Vec3 dir = VNorm(to_target);
    for (int i = 1; i < n; ++i)
      x[i] = VAdd(x[i - 1], VScale(dir, len[i - 1]));
    if (pole_active) apply_pole();
  }
  for (int pass = 0; pass < max_passes; ++pass) {
    for (int i = 0; i < n; ++i) prev[i] = x[i];
    x[n - 1] = target;
    for (int i = n - 2; i >= 1; --i) {
      const Vec3 v{x[i].x - x[i + 1].x, x[i].y - x[i + 1].y,
                   x[i].z - x[i + 1].z};
      const float l = VLen(v);
      x[i] = l > 1e-9f ? VAdd(x[i + 1], VScale(v, len[i] / l))
                       : VAdd(x[i + 1], Vec3{len[i], 0, 0});
    }
    if (pole_active) apply_pole();
    x[0] = root;
    for (int i = 1; i < n; ++i) {
      const Vec3 v{x[i].x - x[i - 1].x, x[i].y - x[i - 1].y,
                   x[i].z - x[i - 1].z};
      const float l = VLen(v);
      x[i] = l > 1e-9f ? VAdd(x[i - 1], VScale(v, len[i - 1] / l))
                       : VAdd(x[i - 1], Vec3{len[i - 1], 0, 0});
    }
    if (pole_active) apply_pole();
    float moved = 0.f;
    for (int i = 1; i < n; ++i) {
      const Vec3 v{x[i].x - prev[i].x, x[i].y - prev[i].y,
                   x[i].z - prev[i].z};
      moved = moved > VLen(v) ? moved : VLen(v);
    }
    if (pass >= 1 && moved < 1e-5f) break;
  }
  for (int i = 0; i < n; ++i)
    if (!FiniteVec(x[i])) { out = ChainIkSolution{}; return out; }
  for (int i = 0; i < n; ++i) out.joints[i] = x[i];
  out.joint_count = n;
  out.bend_dir = bend_dir;
  out.plane_normal = plane_normal;
  out.valid = true;
  return out;
}

// Three-joint chains map 1:1 onto the retained IkSolution contract so the
// arm_pose_math.h transport (plane-based deltas, hinge aim) stays unchanged.
inline IkSolution IkSolutionFromChain(const ChainIkSolution& chain) {
  IkSolution out;
  if (!chain.valid || chain.joint_count != 3) return out;
  const Vec3& s = chain.joints[0];
  const Vec3& e = chain.joints[1];
  const Vec3& w = chain.joints[2];
  out.shoulder = s;
  out.elbow = e;
  out.wrist = w;
  out.upper_dir = VNorm(Vec3{e.x - s.x, e.y - s.y, e.z - s.z});
  out.forearm_dir = VNorm(Vec3{w.x - e.x, w.y - e.y, w.z - e.z});
  out.bend_dir = chain.bend_dir;
  out.plane_normal = chain.plane_normal;
  out.reached = chain.reached;
  out.valid = FiniteVec(out.upper_dir) && FiniteVec(out.forearm_dir) &&
              FiniteVec(out.bend_dir) && FiniteVec(out.plane_normal);
  return out;
}

// Shortest-arc quaternion (xyzw) rotating unit `from` onto unit `to`.
inline void QuatFromTo(Vec3 from, Vec3 to, float out_xyzw[4]) {
  // Invalid/rest-axis input must never yield a non-unit quaternion that
  // later corrupts a verified bone transform.
  out_xyzw[0] = out_xyzw[1] = out_xyzw[2] = 0.0f;
  out_xyzw[3] = 1.0f;
  if (!FiniteVec(from) || !FiniteVec(to) ||
      !(VLen(from) > 1e-6f) || !(VLen(to) > 1e-6f) ||
      !std::isfinite(VLen(from)) || !std::isfinite(VLen(to))) return;
  Vec3 f = VNorm(from), t = VNorm(to);
  float d = VDot(f, t);
  if (d > 0.99999f) {
    out_xyzw[0] = out_xyzw[1] = out_xyzw[2] = 0.0f;
    out_xyzw[3] = 1.0f;
    return;
  }
  if (d < -0.99999f) {  // opposite: rotate 180 deg about any perpendicular
    Vec3 arb = std::fabs(f.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    Vec3 axis = VNorm(VCross(f, arb));
    out_xyzw[0] = axis.x; out_xyzw[1] = axis.y; out_xyzw[2] = axis.z;
    out_xyzw[3] = 0.0f;
    return;
  }
  Vec3 axis = VCross(f, t);
  float s = std::sqrt((1.0f + d) * 2.0f);
  out_xyzw[0] = axis.x / s; out_xyzw[1] = axis.y / s;
  out_xyzw[2] = axis.z / s; out_xyzw[3] = s * 0.5f;
}

// Frame-rate independent exponential smoothing (lambda ~18 = responsive
// without hiding the hand's own motion; no smoothing across teleports
// is the caller's job via the velocity teleport guard).
inline Vec3 ExpSmooth(Vec3 current, Vec3 target, float dt, float lambda) {
  if (!std::isfinite(dt) || !std::isfinite(lambda) || !(dt > 0.0f) ||
      !(lambda >= 0.0f) || !FiniteVec(current) || !FiniteVec(target))
    return target;
  float k = 1.0f - std::exp(-lambda * dt);
  return VAdd(current, VScale(Vec3{target.x - current.x,
                                   target.y - current.y,
                                   target.z - current.z}, k));
}

}  // namespace dsvr
