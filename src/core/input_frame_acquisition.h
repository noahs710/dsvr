#pragma once

#include <openxr/openxr.h>

#include "input.h"

namespace dsvr {

struct OpenXrInputBindings {
  XrSession session = XR_NULL_HANDLE;
  XrActionSet action_set = XR_NULL_HANDLE;
  XrAction squeeze = XR_NULL_HANDLE;
  XrAction trigger = XR_NULL_HANDLE;
  XrAction stick = XR_NULL_HANDLE;
  XrAction primary = XR_NULL_HANDLE;
  XrAction secondary = XR_NULL_HANDLE;
  XrAction menu = XR_NULL_HANDLE;
  XrAction stick_click = XR_NULL_HANDLE;
  XrAction aim_pose = XR_NULL_HANDLE;
  XrPath hand_paths[2] = {XR_NULL_PATH, XR_NULL_PATH};
  XrSpace grip_spaces[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
  XrSpace aim_spaces[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
  XrSpace view_space = XR_NULL_HANDLE;
};

struct AcquiredHand {
  bool grip_valid = false;
  bool grip_tracking_flags_available = false;
  bool grip_position_tracked = false;
  bool grip_orientation_tracked = false;
  float grip_position[3] = {0, 0, 0};
  float grip_orientation[4] = {0, 0, 0, 1};
  bool aim_valid = false;
  float aim_position[3] = {0, 0, 0};
  float aim_orientation[4] = {0, 0, 0, 1};
  bool linear_velocity_valid = false;
  bool angular_velocity_valid = false;
  unsigned velocity_flags = 0;
  float linear_velocity[3] = {0, 0, 0};
  float angular_velocity[3] = {0, 0, 0};
};

struct OpenXrInputFrame {
  VrPadInput actions{};
  AcquiredHand hands[2]{};
  std::uint64_t space_epoch = 0;
  XrTime predicted_display_time = 0;
  bool pad_active = false;
  bool right_stick_active = false;
  bool right_stick_failed = false;
  bool view_orientation_valid = false;
  float view_orientation[4] = {0, 0, 0, 1};
};

enum class OpenXrInputFrameStatus { Ready, NotFocused, SyncFailed };

struct OpenXrInputFrameResult {
  OpenXrInputFrameStatus status = OpenXrInputFrameStatus::SyncFailed;
  XrResult sync_result = XR_ERROR_RUNTIME_FAILURE;
};

// Acquire one immutable action/pose snapshot. Policy, history updates and pad
// routing belong to InputPollFrame; this boundary only talks to OpenXR.
OpenXrInputFrameResult AcquireOpenXrInputFrame(
    const OpenXrInputBindings& bindings, XrSpace base_space,
    std::uint64_t space_epoch, XrTime predicted_display_time,
    bool acquire_view_orientation, OpenXrInputFrame& out);

}  // namespace dsvr
