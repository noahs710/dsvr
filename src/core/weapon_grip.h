#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "arm_pose_math.h"

namespace dsvr {

// Offsets relative to the equipped weapon's native grip, never world space.
// Euler degrees apply X, then Y, then Z. Zero preserves the native placement.
struct WeaponGripPlacement {
  Vec3 position_m{};
  Vec3 rotation_deg{};
};
struct WeaponGripProfile {
  int weapon_id = -1;
  bool dominant_left = false;
  WeaponGripPlacement primary{};
  WeaponGripPlacement support{{0, 0, 0.12f}, {}};
  bool support_enabled = true;
};

// Startup/explicit menu commands only: no disk work in the pose callback.
// A malformed file is refused as a whole, preserving the previous live profiles.
bool WeaponGripsInitialize(const std::wstring& path);
bool WeaponGripGet(int weapon_id, bool dominant_left, WeaponGripProfile& out);
std::vector<WeaponGripProfile> WeaponGripProfiles();
bool WeaponGripSave(const WeaponGripProfile& profile);
bool WeaponGripReset(int weapon_id, bool dominant_left);
bool WeaponGripValid(const WeaponGripProfile& profile);
bool WeaponGripPreview(const WeaponGripProfile& profile);
void WeaponGripClearPreview();

// Apply a saved primary placement to the pose adapter's existing native grip.
// desired_hand supplies the baseline orientation; grip_target is the tracked
// native attachment target. Only those outputs change, never root/physics.
bool WeaponGripApplyPrimary(const WeaponGripPlacement& placement,
                            const ArmPoseMatrix& reference_hand,
                            const ArmPoseMatrix& reference_weapon,
                            float model_units_per_metre,
                            ArmPoseMatrix& desired_hand, Vec3& grip_target);
// Trim the attached support wrist around the solved weapon's local axes.
// Zero keeps the calibrated controller orientation.
bool WeaponGripApplySupportRotation(Vec3 degrees, const ArmPoseMatrix& weapon,
                                    ArmPoseMatrix& desired_hand);

struct WeaponTwoHandState {
  bool active = false;
  bool must_release = false;
  int weapon_id = -1;
  bool dominant_left = false;
  std::uint64_t binding_epoch = 0;
  void Reset() { *this = {}; }
};
// A visual rigid-weapon constraint, not native two-hand damage/stat activation.
// Hold the support grip within 14 cm of the saved handle to attach. Rotation
// follows both hands, while handle separation/weapon reach remain fixed.
bool WeaponGripSolveTwoHand(const WeaponGripProfile& profile,
                            const ArmPoseMatrix& reference_hand,
                            const ArmPoseMatrix& reference_weapon,
                            float model_units_per_metre, Vec3 primary_anchor,
                            Vec3 offhand_target, bool offhand_grip,
                            std::uint64_t binding_epoch, WeaponTwoHandState& state,
                            ArmPoseMatrix& desired_primary_hand,
                            Vec3& weapon_grip_target, Vec3& support_grip_target);

}  // namespace dsvr
