#include "../src/core/input_frame_acquisition.h"

#include <cstdio>
#include <cstdint>

namespace {
unsigned failures = 0;
unsigned sync_calls = 0;
unsigned state_calls = 0;
unsigned locate_calls = 0;
XrResult sync_result = XR_SUCCESS;
XrSpace expected_base = XR_NULL_HANDLE;
XrTime expected_time = 0;

template <typename T>
T Handle(std::uintptr_t value) { return reinterpret_cast<T>(value); }

void Check(bool condition, const char* message) {
  if (!condition) {
    std::printf("FAIL: %s\n", message);
    ++failures;
  }
}
}

extern "C" {
XRAPI_ATTR XrResult XRAPI_CALL xrSyncActions(XrSession,
    const XrActionsSyncInfo* info) {
  ++sync_calls;
  Check(info && info->countActiveActionSets == 1 &&
        info->activeActionSets[0].actionSet == Handle<XrActionSet>(2),
        "sync uses the acquired action set");
  return sync_result;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateFloat(XrSession,
    const XrActionStateGetInfo* info, XrActionStateFloat* out) {
  ++state_calls;
  out->isActive = XR_TRUE;
  out->currentState = info->action == Handle<XrAction>(11) ? .8f : .95f;
  return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateBoolean(XrSession,
    const XrActionStateGetInfo* info, XrActionStateBoolean* out) {
  ++state_calls;
  out->isActive = XR_TRUE;
  out->currentState = info->action == Handle<XrAction>(14) ||
      info->action == Handle<XrAction>(15) ||
      info->action == Handle<XrAction>(16) ||
      info->action == Handle<XrAction>(17) ? XR_TRUE : XR_FALSE;
  return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateVector2f(XrSession,
    const XrActionStateGetInfo* info, XrActionStateVector2f* out) {
  ++state_calls;
  out->isActive = XR_TRUE;
  out->currentState = info->subactionPath == static_cast<XrPath>(21)
      ? XrVector2f{.25f, -.5f} : XrVector2f{-.75f, .5f};
  return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStatePose(XrSession,
    const XrActionStateGetInfo*, XrActionStatePose* out) {
  ++state_calls;
  out->isActive = XR_TRUE;
  return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrLocateSpace(XrSpace space, XrSpace base,
    XrTime time, XrSpaceLocation* out) {
  ++locate_calls;
  Check(base == expected_base && time == expected_time,
        "every pose uses the requested base space and predicted time");
  out->locationFlags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT |
      XR_SPACE_LOCATION_POSITION_VALID_BIT;
  if (space == Handle<XrSpace>(30)) {
    out->pose.orientation = {0, 0, 0, 1};
    out->locationFlags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    return XR_SUCCESS;
  }
  const bool aim = space == Handle<XrSpace>(31) || space == Handle<XrSpace>(32);
  const int side = space == Handle<XrSpace>(31) || space == Handle<XrSpace>(41) ? 0 : 1;
  out->pose.position = {aim ? float(side + 1) : float(side + 3), 2, 3};
  out->pose.orientation = {0, 0, 0, 1};
  if (!aim && out->next) {
    auto* velocity = static_cast<XrSpaceVelocity*>(out->next);
    velocity->velocityFlags = XR_SPACE_VELOCITY_LINEAR_VALID_BIT |
        XR_SPACE_VELOCITY_ANGULAR_VALID_BIT;
    velocity->linearVelocity = {1, 2, 3};
    velocity->angularVelocity = {.1f, .2f, .3f};
  }
  return XR_SUCCESS;
}
}

int main() {
  using namespace dsvr;
  OpenXrInputBindings bindings{};
  bindings.session = Handle<XrSession>(1);
  bindings.action_set = Handle<XrActionSet>(2);
  bindings.squeeze = Handle<XrAction>(11);
  bindings.trigger = Handle<XrAction>(12);
  bindings.stick = Handle<XrAction>(13);
  bindings.primary = Handle<XrAction>(14);
  bindings.secondary = Handle<XrAction>(15);
  bindings.menu = Handle<XrAction>(16);
  bindings.stick_click = Handle<XrAction>(17);
  bindings.aim_pose = Handle<XrAction>(18);
  bindings.hand_paths[0] = static_cast<XrPath>(21);
  bindings.hand_paths[1] = static_cast<XrPath>(22);
  bindings.grip_spaces[0] = Handle<XrSpace>(41);
  bindings.grip_spaces[1] = Handle<XrSpace>(42);
  bindings.aim_spaces[0] = Handle<XrSpace>(31);
  bindings.aim_spaces[1] = Handle<XrSpace>(32);
  bindings.view_space = Handle<XrSpace>(30);
  expected_base = Handle<XrSpace>(50);
  expected_time = 987654321;

  OpenXrInputFrame frame{};
  const auto ready = AcquireOpenXrInputFrame(bindings, expected_base, 13,
      expected_time, true, frame);
  Check(ready.status == OpenXrInputFrameStatus::Ready &&
        ready.sync_result == XR_SUCCESS, "focused frame is acquired");
  Check(sync_calls == 1 && locate_calls == 5,
        "one sync acquires head, two aims and two grips");
  Check(frame.pad_active && frame.right_stick_active &&
        !frame.right_stick_failed, "action activity and stick status preserved");
  Check(frame.actions.left.stick_x == .25f &&
        frame.actions.left.stick_y == -.5f &&
        frame.actions.left.squeeze == .8f &&
        frame.actions.left.trigger == .95f &&
        frame.actions.left.menu && frame.actions.left.secondary,
        "left raw actions are acquired without policy remapping");
  Check(frame.actions.right.stick_x == -.75f &&
        frame.actions.right.stick_y == .5f &&
        frame.actions.right.primary && frame.actions.right.stick_click,
        "right raw actions are acquired without policy remapping");
  Check(frame.view_orientation_valid && frame.view_orientation[3] == 1.0f,
        "requested view orientation is acquired");
  Check(frame.hands[0].grip_valid && frame.hands[1].grip_valid &&
        frame.hands[0].grip_position[0] == 3.0f && frame.hands[1].grip_position[0] == 4.0f &&
        frame.hands[0].aim_valid && frame.hands[0].aim_position[0] == 1.0f &&
        frame.hands[1].aim_valid && frame.hands[1].aim_position[0] == 2.0f,
        "grip and aim poses remain separate in the acquisition snapshot");
  Check(frame.space_epoch == 13 &&
        frame.predicted_display_time == expected_time &&
        frame.hands[0].linear_velocity_valid &&
        frame.hands[0].angular_velocity_valid &&
        frame.hands[0].velocity_flags ==
        (XR_SPACE_VELOCITY_LINEAR_VALID_BIT | XR_SPACE_VELOCITY_ANGULAR_VALID_BIT),
        "acquisition retains frame/velocity provenance without applying hand policy");

  sync_result = XR_SESSION_NOT_FOCUSED;
  frame.pad_active = true;
  const unsigned calls_before_unfocused = state_calls;
  const auto unfocused = AcquireOpenXrInputFrame(bindings, expected_base, 13,
      expected_time, true, frame);
  Check(unfocused.status == OpenXrInputFrameStatus::NotFocused &&
        unfocused.sync_result == XR_SESSION_NOT_FOCUSED,
        "unfocused sync returns a distinct acquisition status");
  Check(!frame.pad_active && !frame.hands[0].grip_valid &&
        state_calls == calls_before_unfocused,
        "unfocused acquisition clears snapshot and skips action queries");

  sync_result = XR_ERROR_RUNTIME_FAILURE;
  const auto failed = AcquireOpenXrInputFrame(bindings, expected_base, 13,
      expected_time, false, frame);
  Check(failed.status == OpenXrInputFrameStatus::SyncFailed &&
        failed.sync_result == XR_ERROR_RUNTIME_FAILURE,
        "sync failure remains distinct for frame policy to handle");
  return failures == 0 ? 0 : 1;
}
