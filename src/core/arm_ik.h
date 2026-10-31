#pragma once
#include "arm_pose_math.h"
#include <cstdint>

namespace dsvr {

struct TrackingFrame;

// IK arm driver (game thread): FABRIK with rigid palette transport drives the
// player's own arms from the fresh native post-animation source pose.
//
// Called after camera application to bind that exact tracking packet and
// calibrated heading. The hook wraps PlayerIns vtable slot +0xA8 and runs
// after its per-bone writer, BEFORE its natural remap/proportion body copy.
// Distinct source/body palettes are supported only through that verified native
// copy; only owned solved rows differ from animation. Publication rechecks
// tracking/calibration, equipment and rig ownership after native code returns.
// Refusal requires a fresh solve, never a guessed copy/replay or root write.
// Log-only remains the default.
// Grip and leg orientation derive from the loaded c0000 FLVER pose. Headset
// body-quality proof is explicitly unverified until the guided user session.
// Physical damage is unfinished.
void ArmIkTick(const TrackingFrame& frame);
bool ArmIkHookInstall();
void ArmIkHookUninstall();
// Historical name: clears transient pose publication only, NEVER user calibration.
void ArmIkResetCalibration();
// Natural game/camera owner only. Read-only, fresh native-chain geometry for
// automatic world/avatar fit; refuses stale/replaced models. Never a mesh scale.
bool ArmIkNativeReach(float& native_reach, std::uint64_t& model_revision);
// Same guarded chain and root scale: the upright native Head bone height above
// the model origin and the native shoulder-socket spread, in world units. The
// head fit anchors the camera on the avatar's own head with these; they are
// never written back as a mesh, bone-base or collider scale.
bool ArmIkNativeStature(float& head_above_origin, float& shoulder_width,
                        std::uint64_t& model_revision);

struct ArmIkHand {
  bool valid=false;
  int weapon_id=-1;
  ArmPoseMatrix hand{},weapon{};
};
struct ArmIkHands {
  std::uint64_t sequence=0,epoch=0,captured_ms=0;
  std::uint64_t calibration_revision=0;
  ArmPoseMatrix root{};
  ArmIkHand side[2];  // physical left/right, native model-space poses
};
// Published only after a guarded native pose commit. Root transforms model
// points to world. Tracking, calibration, mode and capture changes invalidate
// the receipt until a fresh natural copy; getters never write native poses.
bool ArmIkLatestHands(ArmIkHands& out);



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
  // Iteration 2: poll-thread haptic drain. ArmIkCombatBench only fires when
  // the Combat panel polls; the render-thread poll frame calls this every
  // frame so queued bench pulses fire without opening the menu.
  void ArmIkDrainBenchHaptics();

  // F2 live-probe seam: programmatic copy-publication counts behind the
  // "native body copy verified=..." log line. Read-only snapshot for the
  // runbook "Verified native copy publication" row; never a publication
  // control or a native write.
  struct ArmIkCopyPublication {
    std::uint64_t commits = 0;      // verified native copies published
    std::uint64_t refusals = 0;     // ready copies refused by VerifyNaturalCopy
    std::uint64_t last_commit_sequence = 0;  // tracking sequence of last commit
    bool verified = false;          // a verified copy is currently live
  };
  void ArmIkCopyPublicationStatus(ArmIkCopyPublication& out);

}  // namespace dsvr
