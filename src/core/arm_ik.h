#pragma once
#include "arm_pose_math.h"
#include <cstdint>

namespace dsvr {

struct TrackingFrame;

// IK arm driver (game thread). Approved design 2026-10-13: the player
// model's own arms are intended to follow controllers via two-bone IK.
// Native positions are solved on the player's own post-animation CPU poses.
//
// Called after camera application to bind that exact tracking packet and
// calibrated heading. The hook wraps PlayerIns vtable slot +0xA8 and runs
// after its per-bone writer. Runtime pose writes additionally require the
// writer output to alias the selected ChrModel palette; only solved matrices
// differ from the just-written animation. Log-only remains the default.
// Grip and leg orientation derive from the loaded c0000 FLVER pose. Headset
// body-quality proof is explicitly unverified until the guided user session.
// Physical damage is unfinished.
void ArmIkTick(const TrackingFrame& frame);
bool ArmIkHookInstall();
void ArmIkHookUninstall();
// Historical name: clears transient pose publication only, NEVER user calibration.
void ArmIkResetCalibration();

struct ArmIkHand {
  bool valid=false;
  int weapon_id=-1;
  ArmPoseMatrix hand{},weapon{};
};
struct ArmIkHands {
  std::uint64_t sequence=0,epoch=0,captured_ms=0;
  ArmPoseMatrix root{};
  ArmIkHand side[2];  // physical left/right, native model-space poses
};
// Published only after a guarded native pose commit. Root transforms model
// points to world. Freshness/sequence/space/menu checks fail closed.
bool ArmIkLatestHands(ArmIkHands& out);

// Diagnostic world-space displacement of the solved head versus native
// animation, from the same guarded commit. It must not feed the camera:
// that feedback loop detached the viewpoint from the body in live testing.
// Same freshness/sequence/space contract as ArmIkLatestHands.
bool ArmIkHeadWorldDelta(Vec3& delta_world, std::uint64_t& sequence,
                         std::uint64_t& epoch);

  // Live combat proving-ground snapshot for the Combat test panel. Solved
  // blade segments per hand (world space, from the committed native pose),
  // tip speeds, the last classified contact (hit/block/parry + damage
  // scale, geometry only, never native damage), and the hand-separation
  // no-clip flag. Fails closed when the bench is off or no commit is fresh.
  struct CombatBenchSnapshot {
    bool bench_on = false;
    bool fresh = false;
    Vec3 grip[2]{};
    Vec3 tip[2]{};
    float tip_speed[2]{};
    int last_event = 0;  // 0 none, 1 hit, 2 block, 3 parry
    int last_target = -1;
    float last_scale = 0.0f;
    bool hands_too_close = false;
    float hand_separation_m = 0.0f;
  };
  bool ArmIkCombatBench(CombatBenchSnapshot& out);

}  // namespace dsvr
