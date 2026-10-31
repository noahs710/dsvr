#include "input_frame_acquisition.h"

#include <cmath>

namespace dsvr {
namespace {

float ReadFloat(XrSession session, XrAction action, XrPath subpath,
                bool& active) {
  XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
  get.action = action;
  get.subactionPath = subpath;
  XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};
  active = false;
  if (XR_FAILED(xrGetActionStateFloat(session, &get, &state))) return 0.0f;
  active = state.isActive != XR_FALSE;
  return active ? state.currentState : 0.0f;
}

bool ReadBool(XrSession session, XrAction action, XrPath subpath,
              bool& active) {
  XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
  get.action = action;
  get.subactionPath = subpath;
  XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
  active = false;
  if (XR_FAILED(xrGetActionStateBoolean(session, &get, &state))) return false;
  active = state.isActive != XR_FALSE;
  return active && state.currentState != XR_FALSE;
}

XrVector2f ReadStick(XrSession session, XrAction action, XrPath subpath,
                     bool& active, bool& query_failed) {
  XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
  get.action = action;
  get.subactionPath = subpath;
  XrActionStateVector2f state{XR_TYPE_ACTION_STATE_VECTOR2F};
  active = false;
  const XrResult result = xrGetActionStateVector2f(session, &get, &state);
  query_failed = XR_FAILED(result);
  if (query_failed) return {};
  active = state.isActive != XR_FALSE;
  return active ? state.currentState : XrVector2f{};
}

bool ReadViewOrientation(XrSpace view_space, XrSpace base_space, XrTime time,
                         float orientation[4]) {
  if (!view_space) return false;
  XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
  if (XR_FAILED(xrLocateSpace(view_space, base_space, time, &location)) ||
      !(location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))
    return false;
  orientation[0] = location.pose.orientation.x;
  orientation[1] = location.pose.orientation.y;
  orientation[2] = location.pose.orientation.z;
  orientation[3] = location.pose.orientation.w;
  float norm_squared = 0.0f;
  for (float value : {orientation[0], orientation[1], orientation[2], orientation[3]}) {
    if (!std::isfinite(value)) return false;
    norm_squared += value * value;
  }
  return norm_squared > 0.5f && norm_squared < 1.5f;
}

bool ReadAimPose(const OpenXrInputBindings& bindings, int side,
                 XrSpace base_space, XrTime time, AcquiredHand& sample) {
  if (!bindings.aim_spaces[side]) return false;
  XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
  get.action = bindings.aim_pose;
  get.subactionPath = bindings.hand_paths[side];
  XrActionStatePose state{XR_TYPE_ACTION_STATE_POSE};
  XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
  constexpr auto required = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT |
      XR_SPACE_LOCATION_POSITION_VALID_BIT;
  if (XR_FAILED(xrGetActionStatePose(bindings.session, &get, &state)) ||
      !state.isActive ||
      XR_FAILED(xrLocateSpace(bindings.aim_spaces[side], base_space, time, &location)) ||
      (location.locationFlags & required) != required) return false;
  sample.aim_position[0] = location.pose.position.x;
  sample.aim_position[1] = location.pose.position.y;
  sample.aim_position[2] = location.pose.position.z;
  sample.aim_orientation[0] = location.pose.orientation.x;
  sample.aim_orientation[1] = location.pose.orientation.y;
  sample.aim_orientation[2] = location.pose.orientation.z;
  sample.aim_orientation[3] = location.pose.orientation.w;
  float norm_squared = 0.0f;
  for (float value : {sample.aim_orientation[0], sample.aim_orientation[1],
                      sample.aim_orientation[2], sample.aim_orientation[3]}) {
    if (!std::isfinite(value)) return false;
    norm_squared += value * value;
  }
  sample.aim_valid =
      std::isfinite(sample.aim_position[0]) && std::isfinite(sample.aim_position[1]) &&
      std::isfinite(sample.aim_position[2]) && norm_squared > 0.5f &&
      norm_squared < 1.5f;
  return sample.aim_valid;
}

void ReadGripPose(const OpenXrInputBindings& bindings, int side,
                  XrSpace base_space, XrTime time, AcquiredHand& sample) {
  XrSpaceVelocity velocity{XR_TYPE_SPACE_VELOCITY};
  XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
  location.next = &velocity;
  const XrResult result = xrLocateSpace(bindings.grip_spaces[side], base_space,
                                        time, &location);
  constexpr auto required = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT |
      XR_SPACE_LOCATION_POSITION_VALID_BIT;
  if (XR_FAILED(result) || (location.locationFlags & required) != required)
    return;
  sample.grip_valid = true;
  sample.grip_position[0] = location.pose.position.x;
  sample.grip_position[1] = location.pose.position.y;
  sample.grip_position[2] = location.pose.position.z;
  sample.grip_orientation[0] = location.pose.orientation.x;
  sample.grip_orientation[1] = location.pose.orientation.y;
  sample.grip_orientation[2] = location.pose.orientation.z;
  sample.grip_orientation[3] = location.pose.orientation.w;
  sample.velocity_flags = static_cast<unsigned>(velocity.velocityFlags);
  sample.linear_velocity_valid =
      (velocity.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) != 0;
  sample.angular_velocity_valid =
      (velocity.velocityFlags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT) != 0;
  if (velocity.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) {
    sample.linear_velocity[0] = velocity.linearVelocity.x;
    sample.linear_velocity[1] = velocity.linearVelocity.y;
    sample.linear_velocity[2] = velocity.linearVelocity.z;
  }
  if (velocity.velocityFlags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT) {
    sample.angular_velocity[0] = velocity.angularVelocity.x;
    sample.angular_velocity[1] = velocity.angularVelocity.y;
    sample.angular_velocity[2] = velocity.angularVelocity.z;
  }
}

}  // namespace

OpenXrInputFrameResult AcquireOpenXrInputFrame(
    const OpenXrInputBindings& bindings, XrSpace base_space,
    std::uint64_t space_epoch, XrTime predicted_display_time,
    bool acquire_view_orientation, OpenXrInputFrame& out) {
  out = {};
  out.space_epoch = space_epoch;
  out.predicted_display_time = predicted_display_time;
  XrActiveActionSet active{bindings.action_set, XR_NULL_PATH};
  XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
  sync.countActiveActionSets = 1;
  sync.activeActionSets = &active;
  const XrResult sync_result = xrSyncActions(bindings.session, &sync);
  if (sync_result == XR_SESSION_NOT_FOCUSED)
    return {OpenXrInputFrameStatus::NotFocused, sync_result};
  if (XR_FAILED(sync_result))
    return {OpenXrInputFrameStatus::SyncFailed, sync_result};

  if (acquire_view_orientation)
    out.view_orientation_valid = ReadViewOrientation(bindings.view_space,
        base_space, predicted_display_time, out.view_orientation);

  for (int side = 0; side < 2; ++side) {
    VrPadHand& pad_hand = side == 0 ? out.actions.left : out.actions.right;
    bool action_active = false;
    pad_hand.squeeze = ReadFloat(bindings.session, bindings.squeeze,
                                  bindings.hand_paths[side], action_active);
    out.pad_active = out.pad_active || action_active;
    pad_hand.trigger = ReadFloat(bindings.session, bindings.trigger,
                                  bindings.hand_paths[side], action_active);
    out.pad_active = out.pad_active || action_active;
    bool stick_failed = false;
    const XrVector2f stick = ReadStick(bindings.session, bindings.stick,
        bindings.hand_paths[side], action_active, stick_failed);
    if (side == 1) {
      out.right_stick_active = action_active;
      out.right_stick_failed = stick_failed;
    }
    out.pad_active = out.pad_active || action_active;
    pad_hand.stick_x = stick.x;
    pad_hand.stick_y = stick.y;
    pad_hand.primary = ReadBool(bindings.session, bindings.primary,
                                  bindings.hand_paths[side], action_active);
    out.pad_active = out.pad_active || action_active;
    pad_hand.secondary = ReadBool(bindings.session, bindings.secondary,
                                   bindings.hand_paths[side], action_active);
    out.pad_active = out.pad_active || action_active;
    pad_hand.menu = ReadBool(bindings.session, bindings.menu,
                              bindings.hand_paths[side], action_active);
    out.pad_active = out.pad_active || action_active;
    pad_hand.stick_click = ReadBool(bindings.session, bindings.stick_click,
                                     bindings.hand_paths[side], action_active);
    out.pad_active = out.pad_active || action_active;

    AcquiredHand& sample = out.hands[side];
    ReadAimPose(bindings, side, base_space, predicted_display_time, sample);
    ReadGripPose(bindings, side, base_space, predicted_display_time, sample);
  }
  return {OpenXrInputFrameStatus::Ready, sync_result};
}

}  // namespace dsvr
