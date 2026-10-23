#pragma once

#include "calibration_math.h"

namespace dsvr {

// Which native measurement anchors the physical-metre -> native-world ratio.
// Both automatic fits convert the ENTIRE native world/avatar together; neither
// scales a collider, a bone base or a pose basis, and neither resizes a skinned
// mesh (that still needs a verified native render owner).
//   Head: the native Head bone height over the calibrated view height, so the
//         camera lands on the avatar's own head and the body occupies the space
//         the wearer's body does. Reach error is left to the IK naturalness
//         budget instead of hidden by an arm-local gain.
//   Arm:  the native arm length over the measured physical reach, so the hands
//         match exactly and the head may sit off the native head bone.
enum class CalibrationFit { Manual, Arm, Head };

// One camera packet's physical-metre -> native-world conversion. Native arm
// lengths are invariant under animation; no pose basis or gameplay shape is
// scaled. The apparent size of the whole native world/avatar changes together.
// Independent mesh resizing still requires a verified native render owner.
struct CalibrationMapping {
  bool valid = false, fitted = false;
  float world_scale = 1.f;
  float eye_height = 1.65f;  // native units, already converted exactly once
  float world_trim = 1.f;
  // Measured native geometry the fit was derived from, for honest reporting.
  float native_head_height = 0.f;     // native units, upright Head bone
  float native_shoulder_width = 0.f;  // native units, shoulder-socket spread
  CalibrationFit fit = CalibrationFit::Manual;
  std::uint64_t calibration_revision = 0, model_revision = 0;
};

// Upright Head-bone height above the model origin, from one fresh native
// palette in model space. Accepted only for a grounded, upright stance: both
// feet on the model's ground plane, the pelvis at a standing height and the
// Head above it. A crouched, lunging, rolled or airborne palette yields 0 and
// the previous frozen measurement stays authoritative, so no animation frame
// can become the avatar's stature.
inline float CalibrationNativeHeadHeight(Vec3 head, Vec3 pelvis, Vec3 foot_a,
                                         Vec3 foot_b) {
  const bool finite = std::isfinite(head.x) && std::isfinite(head.y) &&
                      std::isfinite(head.z) && std::isfinite(pelvis.x) &&
                      std::isfinite(pelvis.y) && std::isfinite(pelvis.z) &&
                      std::isfinite(foot_a.x) && std::isfinite(foot_a.y) &&
                      std::isfinite(foot_a.z) && std::isfinite(foot_b.x) &&
                      std::isfinite(foot_b.y) && std::isfinite(foot_b.z);
  if (!finite) return 0.f;
  // low/high are the lower and higher foot, not the first and second argument:
  // comparing min against max made the knee-down test below dead code, so a
  // lunging or one-kneeling palette could have become the avatar's stature.
  const float low = std::min(foot_a.y, foot_b.y), high = std::max(foot_a.y, foot_b.y);
  if (!(low >= 0.f) || low > .35f) return 0.f;       // airborne / off the floor
  if (high - low > .35f) return 0.f;                 // one knee down / lunge
  if (!(pelvis.y > .5f) || pelvis.y > 3.5f) return 0.f;
  if (!(head.y > pelvis.y + .25f) || head.y > 4.5f) return 0.f;
  return head.y;
}

// A head fit outside this band means a stale or deformed palette, not a short
// player: fall back to the arm fit rather than making the world enormous.
inline constexpr float kHeadFitScaleMin = .3f, kHeadFitScaleMax = 3.f;

inline CalibrationMapping ResolveCalibrationMapping(
    const UserCalibration& profile, float fallback_eye_height_m, bool seated,
    float world_trim, float native_arm_reach = 0.f,
    std::uint64_t model_revision = 0, float native_head_height = 0.f,
    float native_shoulder_width = 0.f, bool prefer_head_fit = true) {
  CalibrationMapping out;
  if (!std::isfinite(fallback_eye_height_m) || fallback_eye_height_m < .5f ||
      fallback_eye_height_m > 2.5f || !std::isfinite(world_trim) ||
      world_trim < .5f || world_trim > 2.f ||
      !std::isfinite(native_arm_reach) || native_arm_reach < 0.f ||
      !std::isfinite(native_head_height) || native_head_height < 0.f ||
      !std::isfinite(native_shoulder_width) || native_shoulder_width < 0.f ||
      (profile.valid && !CalibrationValidProfile(profile)))
    return out;
  const float view_height_m =
      CalibrationViewHeight(profile, fallback_eye_height_m, seated);
  const bool arm_available = profile.valid && native_arm_reach > 0.f;
  const bool head_available = profile.valid && native_head_height > 0.f;
  if ((arm_available || head_available) && !model_revision) return out;
  if (arm_available && (native_arm_reach < .1f || native_arm_reach > 2.f))
    return out;
  float automatic = 1.f;
  if (head_available) {
    const float candidate = native_head_height / view_height_m;
    if (prefer_head_fit && native_head_height > .4f && native_head_height < 6.f &&
        std::isfinite(candidate) && candidate >= kHeadFitScaleMin &&
        candidate <= kHeadFitScaleMax) {
      automatic = candidate;
      out.fit = CalibrationFit::Head;
    }
  }
  if (out.fit == CalibrationFit::Manual && arm_available) {
    const float physical = (CalibrationReachForScale(profile, 0) +
                            CalibrationReachForScale(profile, 1)) * .5f;
    if (!(physical > 0.f)) return out;
    automatic = native_arm_reach / physical;
    out.fit = CalibrationFit::Arm;
  }
  if (out.fit != CalibrationFit::Manual) {
    out.fitted = true;
    out.model_revision = model_revision;
  }
  out.native_head_height = native_head_height;
  out.native_shoulder_width = native_shoulder_width;
  out.world_scale = automatic * world_trim;
  // The Head fit is anchored on the native head bone, so the camera height in
  // native units is exactly that bone's height by construction; the physical
  // view height still sets the ratio, and the trim still moves the whole world.
  out.eye_height = out.fit == CalibrationFit::Head
                       ? native_head_height * world_trim
                       : view_height_m * out.world_scale;
  out.world_trim = world_trim;
  out.calibration_revision = profile.revision;
  out.valid = std::isfinite(out.world_scale) && out.world_scale > 0.f &&
              std::isfinite(out.eye_height) && out.eye_height > 0.f;
  return out;
}

}  // namespace dsvr
