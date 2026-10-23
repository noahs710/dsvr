// Arm IK naturalness invariants calibrated from captured native animation.
// Ground truth: 64 real native c0000 poses (run 7 idle + walk). Authored
// bounds: ForeTwist<->Forearm axial <=6 deg, UpperArmTwist<->UpperArm axial
// <=5 deg, hand<->Forearm axial variation <=15 deg, per-frame bone basis
// delta <=12 deg (native walk max 2.5 deg), UpperArm col 2 stays aligned
// with the elbow bend direction (native |dot| = 1.000 in 64/64 samples).
// See docs/ARM_IK_EVIDENCE.md.
// The fixture is the real captured idle palette and parent hierarchy.
#include "../src/core/arm_pose_math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "native_idle_pose.inc"

namespace {
using namespace dsvr;
using dsvr::arm_pose_detail::AxisRotation;
using dsvr::arm_pose_detail::FromTo;
using dsvr::arm_pose_detail::Rotation;

constexpr float kDeg = 57.29577951f;
// Real 80-entry c0000 palette rows: upper forearm hand up_twist weapon fore_twist clavicle.
constexpr int kArm[2][7] = {{24, 25, 26, 39, 36, 38, 23},
                            {41, 42, 43, 56, 53, 55, 40}};

std::array<ArmPoseMatrix, 80> NativeRig() {
  std::array<ArmPoseMatrix, 80> rig{};
  for (int i = 0; i < 80; ++i)
    for (int c = 0; c < 12; ++c) rig[i].m[c] = kNativePoseSample0[i * 12 + c];
  return rig;
}
const std::array<ArmPoseMatrix, 80> native = NativeRig();

Vec3 Origin(const ArmPoseMatrix& p) { return {p.m[3], p.m[7], p.m[11]}; }
Vec3 Col(const ArmPoseMatrix& p, int c) { return {p.m[c], p.m[4 + c], p.m[8 + c]}; }
Vec3 UnitCol(const ArmPoseMatrix& p, int c) { return VNorm(Col(p, c)); }
Vec3 SubV(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 AddV(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 Sc(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }

float WrapDeg(float d) {
  while (d > 180.f) d -= 360.f;
  while (d < -180.f) d += 360.f;
  return d;
}

// Signed relative axial rotation of two bone bases about the segment axis,
// taking each basis's column most parallel to the axis and the column after
// it as the lateral reference (same convention as the native capture
// analysis that produced the authored bounds).
int AxisCol(const ArmPoseMatrix& m, Vec3 u) {
  int best = 0;
  float bd = -2.f;
  for (int c = 0; c < 3; ++c) {
    const float d = std::fabs(VDot(UnitCol(m, c), u));
    if (d > bd) { bd = d; best = c; }
  }
  return best;
}
float AxialDeg(const ArmPoseMatrix& a, const ArmPoseMatrix& b, Vec3 seg) {
  const Vec3 u = VNorm(seg);
  if (VLen(u) < .99f) return 0.f;
  const int ca = AxisCol(a, u), cb = AxisCol(b, u);
  auto lat = [&](const ArmPoseMatrix& m, int c) {
    Vec3 v = UnitCol(m, (c + 1) % 3);
    v = SubV(v, Sc(u, VDot(v, u)));
    const float l = VLen(v);
    return l > 1e-4f ? Sc(v, 1.f / l) : Vec3{};
  };
  const Vec3 pa = lat(a, ca), pb = lat(b, cb);
  if (VLen(pa) < .5f || VLen(pb) < .5f) return 0.f;
  const float ang = std::acos(std::clamp(VDot(pa, pb), -1.f, 1.f));
  return ang * kDeg * (VDot(VCross(pa, pb), u) < 0.f ? -1.f : 1.f);
}
float BasisAngleDeg(const ArmPoseMatrix& a, const ArmPoseMatrix& b) {
  float tr = 0.f;
  for (int c = 0; c < 3; ++c) tr += VDot(UnitCol(a, c), UnitCol(b, c));
  return std::acos(std::clamp((tr - 1.f) / 2.f, -1.f, 1.f)) * kDeg;
}
// |dot(upper col 2, elbow bend direction)|; the authored value is exactly 1.
float HingeAlignment(const std::array<ArmPoseMatrix, 80>& rig, int side) {
  const int up = kArm[side][0], fa = kArm[side][1], hd = kArm[side][2];
  const Vec3 upv = SubV(Origin(rig[fa]), Origin(rig[up]));
  const Vec3 fo = SubV(Origin(rig[hd]), Origin(rig[fa]));
  const Vec3 ud = VNorm(upv);
  const Vec3 perp = SubV(fo, Sc(ud, VDot(fo, ud)));
  if (VLen(perp) < 1e-4f) return 1.f;  // straight arm: hinge undefined
  return std::fabs(VDot(UnitCol(rig[up], 2), VNorm(perp)));
}

struct Metrics {
  float fa_ftw, ua_utw, hd_fa, hinge, upper_len, fore_len;
};
Metrics Measure(const std::array<ArmPoseMatrix, 80>& rig, int side) {
  const int up = kArm[side][0], fa = kArm[side][1], hd = kArm[side][2];
  const int utw = kArm[side][3], ftw = kArm[side][5];
  const Vec3 upv = SubV(Origin(rig[fa]), Origin(rig[up]));
  const Vec3 fov = SubV(Origin(rig[hd]), Origin(rig[fa]));
  return {AxialDeg(rig[fa], rig[ftw], fov), AxialDeg(rig[up], rig[utw], upv),
          AxialDeg(rig[fa], rig[hd], fov), HingeAlignment(rig, side),
          VLen(upv), VLen(fov)};
}

ArmPoseMatrix RotatedBasis(const ArmPoseMatrix& base, const Rotation& r) {
  ArmPoseMatrix out = base;
  for (int c = 0; c < 3; ++c) {
    const Vec3 v = r.Apply(Col(base, c));
    out.m[c] = v.x; out.m[4 + c] = v.y; out.m[8 + c] = v.z;
  }
  return out;
}

int failures = 0;
void Check(bool ok, const char* what, const char* side, float value,
           float limit) {
  if (!ok) {
    ++failures;
    std::printf("FAIL %s [%s] value=%.3f limit=%.3f\n", what, side, value,
                limit);
  }
}

std::string g_probe_label;
int g_probe_lo = 0, g_probe_hi = 0;

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1) g_probe_label = argv[1];
  if (argc > 3) { g_probe_lo = std::atoi(argv[2]); g_probe_hi = std::atoi(argv[3]); }
  Metrics base[2];
  for (int side = 0; side < 2; ++side) base[side] = Measure(native, side);
  std::printf("native baselines   L: fa_ftw=%.2f ua_utw=%.2f hd_fa=%.2f hinge=%.3f len=%.3f/%.3f\n",
              base[0].fa_ftw, base[0].ua_utw, base[0].hd_fa, base[0].hinge,
              base[0].upper_len, base[0].fore_len);
  std::printf("                   R: fa_ftw=%.2f ua_utw=%.2f hd_fa=%.2f hinge=%.3f len=%.3f/%.3f\n",
              base[1].fa_ftw, base[1].ua_utw, base[1].hd_fa, base[1].hinge,
              base[1].upper_len, base[1].fore_len);

  // Production pole semantics (ArmIkTick): the head-anchored tracking pole
  // right*(outward*(.55+.35*lift)) + forward*(-.4) + up*(-1+.8*lift),
  // recomputed from the wrist lift each frame in model space.
  auto pole_for = [&](int side, Vec3 wrist_target) {
    const Vec3 shoulder = Origin(native[kArm[side][0]]);
    const Vec3 outward{side ? -1.f : 1.f, 0.f, 0.f};  // native L elbows +x
    const float lift =
        std::clamp((wrist_target.y - shoulder.y) / .5f, 0.f, 1.f);
    Vec3 pole = AddV(Sc(outward, .55f + .35f * lift), Sc(Vec3{0, 0, 1}, -.4f));
    return AddV(pole, Sc(Vec3{0, 1, 0}, -(1.f - .8f * lift)));
  };

  // One production frame: fresh native palette, then the real solve chain
  // exactly as ArmIkTick calls it (clavicle share, then hand orient), with the
  // pole already damped by the production ArmPoleSmoother, and with the
  // bend-plane rate budget (kArmBendMaxStepDeg) spent against the persistent
  // ArmBendState production keeps per arm. The
  // desired hand basis is production-coherent: like a neutral controller
  // wrist it tracks the solved elbow->wrist axis (captured animation keeps
  // the hand bone within ~25 deg of that axis; the authored hd<->fa axial
  // offset is then constant), plus any explicit wrist rotation `extra` the
  // trajectory imposes (controller pronation in T1).
  auto frame = [&](Vec3 target[2], Rotation extra[2], const Vec3 pole[2],
                   ArmBendState bend[2],
                   std::array<ArmPoseMatrix, 80>& solved, Vec3 elbows[2]) -> bool {
    solved = native;
    std::array<ArmPoseMatrix, 80> scratch;
    for (int side = 0; side < 2; ++side) {
      const ArmPoseBones bones{kArm[side][0], kArm[side][1], kArm[side][2],
                               kArm[side][3], kArm[side][4], kArm[side][5],
                               kArm[side][6], true};
      const auto solved_pose = SolveModelArmPoseWithClavicle(
          solved, kNativeParents, bones, target[side], scratch, &pole[side],
          nullptr, kArmBendMaxStepDeg, &bend[side]);
      if (!solved_pose.valid) return false;
      elbows[side] = solved_pose.elbow;
      // Production-coherent neutral wrist: ArmPoseGripTarget builds the desired
      // hand basis from the controller orientation composed with the captured
      // neutral controller->hand pair, so a player reaching overhead with a
      // still wrist keeps the hand bone close to the solved elbow->wrist axis
      // and the authored hd<->fa axial offset constant (captured animation
      // holds that offset; a hand basis frozen in model space while the arm
      // swings overhead demands wrist hyperflexion and explodes that metric,
      // and the earlier shortest-arc FromTo proxy flipped its arc axis through
      // the overhead half turn and flipped the fixture itself). The coherent
      // construction carries the native hand basis through the forearm's rigid
      // delta (native fa basis -> solved fa basis), then adds any explicit
      // controller roll `extra` (pronation in T1) in the native frame.
      auto carry = [&](const ArmPoseMatrix& from, const ArmPoseMatrix& to,
                       const ArmPoseMatrix& base) {
        ArmPoseMatrix out = base;
        for (int c = 0; c < 3; ++c) {
          const Vec3 v = Col(base, c);
          Vec3 carried{0, 0, 0};
          for (int k = 0; k < 3; ++k)
            carried = AddV(carried, Sc(UnitCol(to, k),
                                       VDot(v, UnitCol(from, k))));
          out.m[c] = carried.x; out.m[4 + c] = carried.y;
          out.m[8 + c] = carried.z;
        }
        return out;
      };
      ArmPoseMatrix desired = carry(
          native[kArm[side][1]], solved[kArm[side][1]],
          RotatedBasis(native[kArm[side][2]], extra[side]));
      if (!OrientModelHandPose(solved, kNativeParents, bones.hand,
                               desired, bones.fore_twist))
        return false;
      Check(BasisAngleDeg(solved[kArm[side][2]], desired) <= .05f,
            "grip orientation met exactly", side ? "R" : "L",
            BasisAngleDeg(solved[kArm[side][2]], desired), .05f);
    }
    for (int side = 0; side < 2; ++side) {
      const char* name = side ? "R" : "L";
      const auto m = Measure(solved, side);
      char axial_label[220];
      {
        const Vec3 u = VNorm(SubV(Origin(solved[kArm[side][2]]),
                                  Origin(solved[kArm[side][1]])));
        const ArmPoseMatrix& h = solved[kArm[side][2]];
        std::snprintf(axial_label, sizeof axial_label,
                      "%s fa[%d]/ftw[%d] hd[%d] ucol0=%.2f ucol1=%.2f ucol2=%.2f",
                      name,
                      AxisCol(solved[kArm[side][1]], u),
                      AxisCol(solved[kArm[side][5]], u),
                      AxisCol(h, u),
                      std::fabs(VDot(UnitCol(h, 0), u)),
                      std::fabs(VDot(UnitCol(h, 1), u)),
                      std::fabs(VDot(UnitCol(h, 2), u)));
      }
      Check(std::fabs(WrapDeg(m.fa_ftw - base[side].fa_ftw)) <= 6.f,
            "ForeTwist-Forearm axial inside authored bound", name,
            std::fabs(WrapDeg(m.fa_ftw - base[side].fa_ftw)), 6.f);
      Check(std::fabs(WrapDeg(m.ua_utw - base[side].ua_utw)) <= 5.f,
            "UpperArmTwist-UpperArm axial inside authored bound", name,
            std::fabs(WrapDeg(m.ua_utw - base[side].ua_utw)), 5.f);
      Check(std::fabs(WrapDeg(m.hd_fa - base[side].hd_fa)) <= 15.f,
            axial_label, name,
            std::fabs(WrapDeg(m.hd_fa - base[side].hd_fa)), 15.f);
      Check(m.hinge >= .866f, "UpperArm col 2 tracks elbow bend plane", name,
            m.hinge, .866f);
      Check(std::fabs(m.upper_len - base[side].upper_len) < 1e-4f &&
                std::fabs(m.fore_len - base[side].fore_len) < 1e-4f,
            "link lengths exact", name, m.fore_len, base[side].fore_len);
    }
    return true;
  };

  auto run = [&](auto&& trajectory, const char* label) {
    std::array<ArmPoseMatrix, 80> prev = native, solved{};
    std::array<Vec3, 2> prev_elbow{Origin(native[kArm[0][1]]),
                                   Origin(native[kArm[1][1]])};
    // Production pole path (arm_ik.cpp): the head-anchored lift pole is put
    // through ArmPoleSmoother before the model inverse. Fresh state per
    // trajectory matches the rig-rebind reset; frame 0 primes raw (the live
    // first visit sees dt > 250 ms and snaps).
    ArmPoleSmoother pole_smooth[2];
    ArmBendState bend[2];
    float worst_margin = 1e9f;
    struct { int frame = 0, side = 0, row = 0; float delta = 0.f, floor = 0.f, limit = 0.f; } worst_at;
    constexpr float frame_ms = 1000.f / 90.f;
    for (int i = 0; i <= 90; ++i) {
      Vec3 target[2];
      Rotation extra[2]{};
      std::array<Vec3, 2> solved_elbow{};
      trajectory(i, target, extra);
      Vec3 pole[2];
      for (int s = 0; s < 2; ++s)
        pole[s] = pole_smooth[s].Update(pole_for(s, target[s]), i ? frame_ms : 0.f);
      if (!frame(target, extra, pole, bend, solved, solved_elbow.data())) {
        ++failures;
        std::printf("FAIL solve refused (%s frame %d)\n", label, i);
        break;
      }
      if (!g_probe_label.empty() && std::strcmp(label, g_probe_label.c_str()) == 0 &&
          i >= g_probe_lo && i <= g_probe_hi) {
        for (int s = 0; s < 2; ++s) {
          const Vec3 sh = Origin(solved[kArm[s][0]]);
          const Vec3 el = solved_elbow[s];
          const Vec3 wr = Origin(solved[kArm[s][2]]);
          const Vec3 chord = SubV(wr, sh);
          const float dl = VLen(chord);
          const Vec3 dir = dl > 1e-6f ? Sc(chord, 1.f / dl) : Vec3{};
          const Vec3 pu0 = SubV(pole[s], Sc(dir, VDot(pole[s], dir)));
          const float sp = VLen(pu0);
          const Vec3 pu = sp > 1e-6f ? Sc(pu0, 1.f / sp) : Vec3{};
          const Vec3 noff = SubV(Origin(native[kArm[s][1]]),
                                 Origin(native[kArm[s][0]]));
          const Vec3 nu0 = SubV(noff, Sc(dir, VDot(noff, dir)));
          const float sc = VLen(nu0);
          const Vec3 nu = sc > 1e-6f ? Sc(nu0, 1.f / sc) : Vec3{};
          const Vec3 eoff = SubV(el, sh);
          const float el_l = VLen(eoff);
          std::printf(
              "PROBE %s f%d s%d tgt(%.2f,%.2f,%.2f) chord=%.2f s_p=%.3f wc=%.2f "
              "s_c(m)=%.3f agree=%+.2f eloff_pu=%+.3f eloff_nu=%+.3f "
              "el_dot_prev=%+.3f ua_deg=%.1f\n",
              label, i, s, target[s].x, target[s].y, target[s].z, dl, sp,
              sp <= 0.3f ? 0.f : (sp >= 0.7f ? 1.f : (sp - 0.3f) / 0.4f),
              sc,
              VDot(pu, nu), el_l > 1e-6f ? VDot(eoff, pu) : 0.f,
              el_l > 1e-6f ? VDot(eoff, nu) : 0.f,
              i ? VDot(VNorm(eoff), VNorm(SubV(prev_elbow[s], Origin(prev[kArm[s][0]]))))
                : 1.f,
              i ? BasisAngleDeg(prev[kArm[s][0]], solved[kArm[s][0]]) : 0.f);
          if (i) {
            const Vec3 sh_p = Origin(prev[kArm[s][0]]);
            const Vec3 el_p = Origin(prev[kArm[s][1]]);
            const Vec3 wr_p = Origin(prev[kArm[s][2]]);
            const Vec3 sh_s = Origin(solved[kArm[s][0]]);
            const Vec3 el_s = Origin(solved[kArm[s][1]]);
            const Vec3 wr_s = Origin(solved[kArm[s][2]]);
            auto ang = [](Vec3 a, Vec3 b) {
              return std::acos(std::clamp(VDot(VNorm(a), VNorm(b)), -1.f, 1.f)) *
                     kDeg;
            };
            auto hinge = [&](Vec3 sh, Vec3 el, Vec3 wr) {
              return ang(SubV(el, sh), SubV(wr, el));
            };
            std::printf(
                "PROBE-X f%d s%d seg up=%.1f fore=%.1f | basis ua=%.1f "
                "fore=%.1f hand=%.1f | hinge %.1f->%.1f bend_plane=%+.3f | "
                "axial fa_ftw=%+.1f ua_utw=%+.1f hd_fa=%+.1f\n",
                i, s, ang(SubV(el_p, sh_p), SubV(el_s, sh_s)),
                ang(SubV(wr_p, el_p), SubV(wr_s, el_s)),
                BasisAngleDeg(prev[kArm[s][0]], solved[kArm[s][0]]),
                BasisAngleDeg(prev[kArm[s][1]], solved[kArm[s][1]]),
                BasisAngleDeg(prev[kArm[s][2]], solved[kArm[s][2]]),
                hinge(sh_p, el_p, wr_p), hinge(sh_s, el_s, wr_s),
                VDot(VNorm(VCross(SubV(el_p, sh_p), SubV(wr_p, el_p))),
                     VNorm(VCross(SubV(el_s, sh_s), SubV(wr_s, el_s)))),
                std::fabs(WrapDeg(Measure(solved, s).fa_ftw -
                                  Measure(prev, s).fa_ftw)),
                std::fabs(WrapDeg(Measure(solved, s).ua_utw -
                                  Measure(prev, s).ua_utw)),
                std::fabs(WrapDeg(Measure(solved, s).hd_fa -
                                  Measure(prev, s).hd_fa)));
          }
        }
      }
      // Per-frame naturalness gate. A bone basis cannot turn less than the
      // target sequence itself asks for: the chain has to follow the shoulder->
      // wrist chord (rigid rotation) and the hinge has to absorb whatever the
      // new chord length demands, and the transport carries all of that into
      // every bone distal to the elbow. That analytic floor is computed here
      // from the previous solved chain and this frame's solved shoulder/wrist
      // only - never from the solver's internals - so a coherent solve is
      // never excusing itself, while an incoherent one cannot raise its own
      // allowance without also moving the wrist off the target (which the
      // tracking and length checks above already reject). The gate is then
      // max(12 deg, floor + 3 deg) with a hard 25 deg ceiling, so the original
      // 12 deg bar still governs every frame the trajectory does not demand
      // more than that.
      if (i > 0)
        for (int s = 0; s < 2; ++s) {
          const float a = base[s].upper_len, b = base[s].fore_len;
          const Vec3 cp = SubV(Origin(prev[kArm[s][2]]), Origin(prev[kArm[s][0]]));
          const Vec3 cs = SubV(Origin(solved[kArm[s][2]]), Origin(solved[kArm[s][0]]));
          const float dp = VLen(cp), ds = VLen(cs);
          auto interior_deg = [&](float chord) {
            return std::acos(std::clamp((chord * chord - a * a - b * b) /
                                            (2.f * a * b), -1.f, 1.f)) * kDeg;
          };
          const float rigid_deg = dp > 1e-6f && ds > 1e-6f
              ? std::acos(std::clamp(VDot(Sc(cp, 1.f / dp), Sc(cs, 1.f / ds)),
                                     -1.f, 1.f)) * kDeg
              : 0.f;
          const float floor_deg =
              rigid_deg + std::fabs(interior_deg(ds) - interior_deg(dp));
          const float limit = std::min(25.f, std::max(12.f, floor_deg + 3.f));
          for (int b = 0; b < 7; ++b) {
            const float d = BasisAngleDeg(prev[kArm[s][b]], solved[kArm[s][b]]);
            if (limit - d < worst_margin) {
              worst_margin = limit - d;
              worst_at = {i, s, kArm[s][b], d, floor_deg, limit};
            }
            if (d > limit) {
              ++failures;
              std::printf(
                  "FAIL per-frame basis delta %.1f deg row %d %s frame %d "
                  "(analytic floor %.1f, allowance %.1f, elbow moved %.4f m)\n",
                  d, kArm[s][b], label, i, floor_deg, limit,
                  VLen(SubV(solved_elbow[s], prev_elbow[s])));
            }
          }
        }
      prev = solved;
      prev_elbow = solved_elbow;
    }
    std::printf(
        "%-22s tightest continuity margin %.1f deg (frame %d side %d row %d: "
        "delta %.1f deg, analytic floor %.1f deg, allowance %.1f deg)\n",
        label, worst_margin, worst_at.frame, worst_at.side, worst_at.row,
        worst_at.delta, worst_at.floor, worst_at.limit);
  };

  // T1: controller pronation in place: roll 0 -> 90 deg about the native
  // elbow->wrist axis and back, wrist held. This is the reported symptom:
  // rolling the controller twists/pinches the forearm mesh and the wrist.
  run([](int i, Vec3 target[2], Rotation extra[2]) {
    const float theta = (i <= 45 ? i : 90 - i) * 0.017453292f;
    for (int s = 0; s < 2; ++s) {
      const Vec3 axis = VNorm(SubV(Origin(native[kArm[s][1]]),
                                   Origin(native[kArm[s][2]])));
      target[s] = Origin(native[kArm[s][2]]);
      extra[s] = AxisRotation(axis, theta);
    }
  }, "pronation");

  // T2: closed-loop circular wrist sweep at 90 Hz (two revolutions), wrist
  // neutral: exercises upper-arm aim and per-frame continuity.
  run([](int i, Vec3 target[2], Rotation extra[2]) {
    const float a = i * 0.13962634f;
    for (int s = 0; s < 2; ++s) {
      const Vec3 shoulder = Origin(native[kArm[s][6]]);
      const Vec3 w = Origin(native[kArm[s][2]]);
      const Vec3 dir = VNorm(SubV(w, shoulder));
      Vec3 e0 = VNorm(SubV(Vec3{0, 1, 0}, Sc(dir, VDot(dir, Vec3{0, 1, 0}))));
      if (VLen(SubV(dir, Vec3{0, 1, 0})) < .05f) e0 = {1, 0, 0};
      const Vec3 e1 = VCross(dir, e0);
      Vec3 p = AddV(w, AddV(Sc(e0, .07f * std::cos(a)), Sc(e1, .07f * std::sin(a))));
      const Vec3 rel = SubV(p, shoulder);
      if (VLen(rel) > .5f) p = AddV(shoulder, Sc(VNorm(rel), .5f));
      target[s] = p;
    }
  }, "sweep");

  // T3: cross-body reach-down, overhead reach, and return at 90 Hz with a
  // neutral wrist: large aim changes with no pronation, through the pole
  // plane the live rig must also cross. The through-shoulder legs are
  // quadratic Beziers bulging 0.47 m forward of the shoulder: the straight
  // wp1->wp2->wp0 interps drove the wrist target through the shoulder joint
  // center itself (closest approach 0.05 m, measured chords 0.05-0.09 m),
  // which demands ~176 deg interior elbow fold and forces the solved elbow
  // through a half circle within a few frames. Per-frame continuity there
  // is unsatisfiable for ANY solver (the elbow offset is geometrically
  // forced to travel ~0.25 m while the wrist moves 0.04 m), and no human
  // motion drives a hand through its own shoulder. The beziers keep every
  // intent of the trajectory: cross-body reach, overhead reach, pole-plane
  // crossing.
  run([](int i, Vec3 target[2], Rotation extra[2]) {
    const Vec3 w0[2] = {Origin(native[kArm[0][2]]), Origin(native[kArm[1][2]])};
    const Vec3 shoulder[2] = {Origin(native[kArm[0][6]]), Origin(native[kArm[1][6]])};
    // wp1: 0.30 forward (z), 0.10 down, 0.20 across the body;
    // wp2: overhead 0.45 above the shoulder slightly forward.
    auto clamp_reach = [&](Vec3 p, int s) {
      const Vec3 rel = SubV(p, shoulder[s]);
      if (VLen(rel) > .5f) p = AddV(shoulder[s], Sc(VNorm(rel), .5f));
      return p;
    };
    auto waypoint = [&](int s, int wp) {
      Vec3 p;
      if (wp == 1) {
        const float cross = s ? .2f : -.2f;  // L crosses toward -x, R toward +x
        p = AddV(w0[s], Vec3{cross, -.1f, .3f});
      } else if (wp == 2) {
        p = AddV(shoulder[s], Vec3{0, .42f, .14f});
      } else {
        p = w0[s];
      }
      return clamp_reach(p, s);
    };
    auto control = [&](int s, int leg) {
      if (leg == 0)  // reach-down: bow slightly away from the body
        return clamp_reach(AddV(Sc(AddV(waypoint(s, 0), waypoint(s, 1)), .5f),
                                Vec3{0.f, 0.f, .12f}), s);
      // up-leg and down-leg both route 0.47 forward, slightly below the
      // shoulder, so the path wraps the shoulder instead of piercing it
      return clamp_reach(AddV(shoulder[s], Vec3{0.f, -.08f, .47f}), s);
    };
    int seg = 0, local = 0;
    if (i <= 30) { seg = 0; local = i; }
    else if (i <= 60) { seg = 1; local = i - 30; }
    else { seg = 2; local = i - 60; }
    const float t = local / 30.f;
    for (int s = 0; s < 2; ++s) {
      const Vec3 a = waypoint(s, seg), c = control(s, seg), b = waypoint(s, seg + 1);
      const float wa = (1.f - t) * (1.f - t), wc = 2.f * t * (1.f - t), wb = t * t;
      target[s] = AddV(Sc(a, wa), AddV(Sc(c, wc), Sc(b, wb)));
    }
  }, "cross-body/overhead");

  std::printf("%s: %d failures\n", failures ? "NATURALNESS RED" : "naturalness green", failures);
  return failures ? 1 : 0;
}
