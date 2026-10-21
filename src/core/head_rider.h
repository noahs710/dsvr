#pragma once

#include <algorithm>
#include <cmath>

#include "camera_math.h"

namespace dsvr {

// The released head matrix is the camera. The model's Head bone is hidden
// by native part filtering, but its origin must end at the eye point so
// the visible neck joins the invisible head continuously. Using one
// helper for every caller avoids two independent notions of neck pose.
//
// The rider turns a desired camera/head offset (world space) into the
// additional translation that must be added to the solved head bone so it
// lands at a fixed eye drop below the rendered camera, expressed in the
// model's root space. `root` is model->world including translation.
// We never ask it to catch the body: desired offset is the whole input,
// the output is clamped to at most `max_shift_m`, and a non-finite input
// produces { 0, 0, 0 } without touching the model.
inline Vec3 HeadRiderOffsetModel(Vec3 desired_world_offset, Vec3 current_world,
                                 const float root_rotation_only[9],
                                 float max_shift_m = 0.35f) {
  if (!(max_shift_m >= 0.0f) || !std::isfinite(max_shift_m) ||
      !std::isfinite(desired_world_offset.x) ||
      !std::isfinite(desired_world_offset.y) ||
      !std::isfinite(desired_world_offset.z) || !std::isfinite(current_world.x) ||
      !std::isfinite(current_world.y) || !std::isfinite(current_world.z))
    return {0, 0, 0};
  Vec3 diff{desired_world_offset.x - current_world.x,
            desired_world_offset.y - current_world.y,
            desired_world_offset.z - current_world.z};
  const float len = VLen(diff);
  if (!std::isfinite(len)) return {0, 0, 0};
  if (len > max_shift_m && len > 1e-6f)
    diff = VScale(diff, max_shift_m / len);
  // World -> model rotation only. root_rotation_only is a row-major 3x3
  // whose columns are the model axes in world space, so the inverse
  // rotation is its transpose. Any uniform model scale is part of the
  // caller's calibration and is not guessed here.
  const float* m = root_rotation_only;
  return {m[0] * diff.x + m[3] * diff.y + m[6] * diff.z,
          m[1] * diff.x + m[4] * diff.y + m[7] * diff.z,
          m[2] * diff.x + m[5] * diff.y + m[8] * diff.z};
}

// Eye point for a bone-origin (pivot) head: the eyes sit slightly forward
// and above the neck pivot. These offsets are in the camera basis.
inline Vec3 HeadEyePoint(Vec3 head_world, Vec3 cam_right, Vec3 cam_up,
                         Vec3 cam_fwd, float up_m = 0.10f,
                         float fwd_m = 0.08f) {
  if (!std::isfinite(up_m) || !std::isfinite(fwd_m)) return head_world;
  return VAdd(VAdd(head_world, VScale(cam_up, up_m)), VScale(cam_fwd, fwd_m));
}

}  // namespace dsvr
