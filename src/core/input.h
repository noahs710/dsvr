#pragma once

#include "combat_math.h"
#include "gesture_math.h"
#include "pad_math.h"
#include "comfort_vignette.h"
#include "interaction_target_math.h"

namespace dsvr {

// Motion input runtime: OpenXR controller actions on the render thread.
// Session/frame ownership stays in xr.cpp; this module only attaches an
// action set to the session xr.cpp created and polls it once per
// submitted frame. Everything fails soft: any runtime without working
// actions keeps the verified mono/headlook path untouched.
//
// IK design (2026-10-13): hands are published CONTINUOUSLY -- poses,
// blade kinematics, per-frame thrust/slash label -- for the IK arm
// driver (arm_ik.cpp) and the future H5 damage consumer. There is no
// gesture classifier and no swing-triggered button pulse: the arm is
// the attack. Game-memory writes never happen here.
//
// Threading: InputAttachSession/InputPollFrame run on the render thread
// (Present detour). Arm targets and pad state cross to the game thread
// under mutexes; turn yaw via an atomic accumulator.

struct HandSample {
  bool valid = false;
  bool tracking_flags_available = false;
  bool position_tracked = false;
  bool orientation_tracked = false;
  float pos[3] = {0, 0, 0};        // grip position, XR LOCAL space (m)
  float quat[4] = {0, 0, 0, 1};    // grip orientation (xyzw)
  float blade_dir[3] = {0, 0, -1}; // unit, hand_math.h convention
  float tip[3] = {0, 0, 0};        // grip + blade_dir * blade_length
  float speed = 0.0f;              // smoothed grip speed (m/s)
  float xr_speed = -1.0f;          // runtime linear velocity, -1 = absent
  float linear_velocity[3] = {0, 0, 0};  // runtime grip velocity, XR local m/s
  float angular_velocity[3] = {0, 0, 0}; // runtime grip velocity, rad/s
  unsigned velocity_flags = 0;      // OpenXR linear/angular validity bits
  bool linear_velocity_valid = false;
  bool angular_velocity_valid = false;
  std::uint64_t space_epoch = 0;
  long long predicted_ns = 0;       // OpenXR time; not QPC
  bool squeeze_held = false;
  bool trigger_held = false;
  bool primary_held = false;
  // Runtime pointing pose, independent from the grip used by IK/equipment.
  bool aim_valid = false;
  float aim_pos[3] = {0, 0, 0};
  float aim_quat[4] = {0, 0, 0, 1};
};

// Continuous arm target for the IK driver / H5 (one per logical hand).
struct ArmTarget {
  bool valid = false;
  bool tracking_flags_available = false;
  bool position_tracked = false;
  bool orientation_tracked = false;
  float pos[3] = {0, 0, 0};        // grip, XR LOCAL (m)
  float quat[4] = {0, 0, 0, 1};
  float blade_dir[3] = {0, 0, -1};
  float tip[3] = {0, 0, 0};
  float tip_speed = 0.0f;          // smoothed blade-tip speed (m/s)
  float grip_speed = 0.0f;
  GestureKind frame_kind = GestureKind::None;  // label only, no trigger
  bool squeeze_held = false;
  bool trigger_held = false;
};


struct RoomscaleBasis {
  Vec3 tracking_right{};
  Vec3 tracking_up{};
  Vec3 tracking_forward{};  // OpenXR -Z forward mapped to native world
  // The frame native interprets artificial travel in: the planar heading of
  // the follow camera the mod actually wrote, never the rendered model's root
  // facing (native turn assist rotates that independently).
  Vec3 movement_basis{};
  std::uint64_t space_epoch = 0;
  std::uint64_t published_ms = 0;
  bool valid = false;
  float world_scale = 1.f;  // same applied camera packet, native units / metre
  std::uint64_t calibration_revision = 0;
};

struct RoomscaleMotion {
  Vec3 world_velocity{};
  std::uint64_t space_epoch = 0;
  std::uint64_t calibration_revision = 0;
  float world_scale = 1.f;
  std::uint64_t published_ms = 0;
  std::uint64_t expires_ms = 0;
  bool active = false;
};

void InputAttachSession(void* xr_instance, void* xr_session);
// head_pos is the same packet's head position (XR tracking space, metres);
// selected hand and VIEW orientations are located in base_space at this same
// predicted display time for locomotion-frame conversion. Blade velocities are
// measured relative to head_pos so physical roomscale
// walking never becomes swing energy. space_epoch is the tracking
// frame's epoch; a change reprimes the blade trackers.
void InputPollFrame(void* base_space, const float head_pos[3],
                    std::uint64_t space_epoch,
                    long long predicted_display_time);
// Render thread: clear stale targets/pad/turns on skipped tracking or loss.
void InputInvalidateFrame();
// Render thread only, immediately after InputPollFrame. Excludes physical follow.
ComfortMotion InputLatestComfortMotion();
// Game thread publishes the applied camera frame: the head steering reference
// and the movement basis native will interpret travel in. Both are the written
// camera's planar heading unless a hand reference overrides the first. Render
// input uses it only for fresh, matching-epoch head-directed locomotion.
void InputPublishHeadDirectedLocomotion(Vec3 head_forward,
                                        Vec3 movement_basis_forward,
                                        std::uint64_t space_epoch);
void InputInvalidateHeadDirectedLocomotion();

void InputPublishRoomscaleBasis(const RoomscaleBasis& basis);

void InputPublishCameraMotionBasis(const RoomscaleBasis& roomscale,
                                   Vec3 head_forward,
                                   Vec3 movement_basis_forward,
                                   std::uint64_t space_epoch);

void InputInvalidateRoomscaleBasis();
bool InputLatestRoomscaleMotion(RoomscaleMotion& out);
// One physical left/right pair, copied after polling for XR publication.
void InputCopyTrackingSamples(HandSample hands[2], ArmTarget arms[2]);

bool InputAvailable();
// True only for a focused, non-menu gameplay frame with both tracked grips
// still and all gameplay inputs neutral. Calibration uses this as a gate.
bool InputCalibrationRefinementSafe();
bool InputLatestHand(bool left, HandSample& out);
// Native selection producer seam (H1): the interaction observer publishes
// the selected row/object/generation/dummy here; the reach gate reads the
// live binding. No producer binds it yet, so the store stays empty.
void InputPublishInteractionSelection(const InteractionSelection& selection,
                                      long long now_ms);
void InputInvalidateInteractionSelection();
// Game thread: latest continuous arm target. want_off_hand=false gives
// the weapon hand (config dominant hand), true the shield/off hand.
bool InputLatestArmTarget(bool want_off_hand, ArmTarget& out);
bool InputLatestPad(VirtualPadState& out);
float InputTakeTurnYaw();
void InputApplyHaptic(bool left, float amplitude, int duration_ms);

// Request a restart notice for edited profile bindings. OpenXR action sets are
// immutable after the one permitted session attachment; keep current input live
// and apply saved profile changes on the next game launch.
void InputRequestBindingReload();

// The profile set the live action set was built with (what the session runs).
// The menu footer compares the applied config against this to show a restart
// notice only after a restart-gated binding change actually lands.
struct InputBindingSignature {
  bool quest3 = true, index = true, wmr = true, simple = true;
};
InputBindingSignature InputLiveBindingSignature();

}  // namespace dsvr
