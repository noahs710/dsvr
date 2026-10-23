// Motion input: OpenXR actions (see input.h). Fresh implementation
// against the OpenXR 1.0 core action API; suggested bindings cover Quest
// Touch, Index, WMR motion controllers, and KHR simple (poses only).
// IK design (2026-10-13): poses feed BladeKinematics CONTINUOUSLY
// (finite-difference inside combat_math.h). No swing is classified into
// a trigger and no button pulse fires -- the IK arm is the attack.

#include "input.h"

#include <windows.h>

#include <openxr/openxr.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>

#include "../common/logging.h"
#include "combo_math.h"
#include "diagnostics.h"
#include "frame_queue.h"
#include "hand_math.h"
#include "mod_menu.h"
#include "turn_math.h"
#include "vr_heading.h"
#include "xr.h"
#include "camera.h"
#include "combat_native.h"

namespace dsvr {
// Calibration owns the coherent TrackingFrame sample in xr.cpp. Input only
// consumes its gate; keeping hand publication active lets that gate finish.
bool CalibrationCapturesInput();
void CalibrationRequest();
namespace {

XrInstance g_instance = XR_NULL_HANDLE;
XrSession g_session = XR_NULL_HANDLE;
XrActionSet g_action_set = XR_NULL_HANDLE;
XrAction g_grip_pose = XR_NULL_HANDLE;
XrAction g_aim_pose = XR_NULL_HANDLE;
XrAction g_squeeze = XR_NULL_HANDLE;
XrAction g_trigger = XR_NULL_HANDLE;
XrAction g_stick = XR_NULL_HANDLE;  // vector2, right hand turns (turn_math)
XrAction g_primary = XR_NULL_HANDLE;
XrAction g_secondary = XR_NULL_HANDLE;
XrAction g_menu = XR_NULL_HANDLE;
XrAction g_stick_click = XR_NULL_HANDLE;
XrAction g_haptic = XR_NULL_HANDLE;
XrPath g_hand_path[2] = {XR_NULL_PATH, XR_NULL_PATH};  // 0 left, 1 right
XrSpace g_grip_space[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
XrSpace g_aim_space[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
XrSpace g_view_space = XR_NULL_HANDLE;  // actual HMD orientation in the input frame
std::atomic<bool> g_attached{false};
std::atomic<bool> g_failed{false};
bool g_logged_unavailable = false;

BladeKinematics g_blade[2];  // per hand, continuous (combat_math.h)
ComboDetector g_combos;  // F-key chords (combo_math.h), render thread only
TurnTracker g_turn;
TurnParams g_turn_params;
bool g_turn_params_init = false;
HandSample g_hand[2];
bool g_hand_logged[2] = {false, false};
std::atomic<float> g_pending_turn_yaw{0.0f};
// Latest VirtualPadState published to the XInput hook (mutex: one writer
// on the render thread, readers on the game's XInput poll thread).
std::mutex g_pad_mutex;
VirtualPadState g_pad_state{};
bool g_pad_active = false;
struct HeadDirectedLocomotion {
  Vec3 head_forward{};
  Vec3 movement_basis{};  // basis native interprets the stick in
  std::uint64_t space_epoch = 0;
  std::uint64_t published_ms = 0;
  bool valid = false;
};
std::mutex g_locomotion_mutex;
HeadDirectedLocomotion g_head_directed_locomotion{};
constexpr std::uint64_t kHeadDirectedLocomotionMaxAgeMs = 100;
MovementOrientation g_active_movement_orientation = MovementOrientation::Head;
int g_active_movement_hand = 0;
bool g_movement_settings_initialized = false;
Vec3 g_last_controller_forward{};
std::uint64_t g_controller_heading_epoch = 0;
int g_controller_heading_hand = -1;
bool g_have_controller_heading = false;
bool g_controller_vertical = false;
bool g_controller_rearm_required = false;
// Latest IK arm targets (mutex: render writer, game-thread reader).
std::mutex g_arm_mutex;
ArmTarget g_arm_target[2];  // 0 left, 1 right
long long g_last_time_ns = 0;
long long g_menu_chord_started_ns = 0;
bool g_menu_chord_fired = false;
long long g_stick_menu_chord_started_ns = 0;
bool g_stick_menu_chord_fired = false;
long long g_recalibration_first_a_ns = 0;
unsigned g_recalibration_a_count = 0;
bool g_recalibration_a_was_down = false;
bool g_recalibration_fired = false;
bool g_capture_rearm = false;
constexpr long long kStickMenuChordNs = 3000000000LL;
constexpr long long kDoubleAClickWindowNs = 700000000LL;
constexpr long long kDoubleAClickMinSpacingNs = 90000000LL;
// Hold both secondary buttons for half a second as a theater shortcut that
// remains available when the runtime reserves the controller's Menu button.
constexpr long long kSanctuaryChordNs = 500000000LL;
// Lead-in: the moment either constituent goes down, both are withheld. A chord
// must never leak its individual press into the game (no accidental dodge or
// heal on the way to theater mode). If the partner never arrives, the lone
// press is released once this window closes.
constexpr long long kChordLeadInNs = 250000000LL;
ButtonPairChord g_sanctuary_chord(kSanctuaryChordNs, kChordLeadInNs);

bool OkInput(XrResult r, const char* what) {
  if (XR_SUCCEEDED(r)) return true;
  char buf[XR_MAX_RESULT_STRING_SIZE]{};
  if (g_instance)
    xrResultToString(g_instance, r, buf);
  else
    snprintf(buf, sizeof(buf), "result=%d", (int)r);
  DSVR_ERROR("Input: %s failed: %s (controllers disabled, VR continues)",
             what, buf);
  return false;
}

bool MakePath(const char* text, XrPath& out) {
  return OkInput(xrStringToPath(g_instance, text, &out), text);
}

bool MakeAction(XrActionType type, const char* name, const char* label,
                bool both_hands, XrAction& out) {
  XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
  info.actionType = type;
  strncpy_s(info.actionName, name, _TRUNCATE);
  strncpy_s(info.localizedActionName, label, _TRUNCATE);
  if (both_hands) {
    info.countSubactionPaths = 2;
    info.subactionPaths = g_hand_path;
  }
  return OkInput(xrCreateAction(g_action_set, &info, &out), name);
}

// One suggested-binding batch for a single interaction profile. Profiles
// without a component simply omit it (KHR simple has no stick/squeeze);
// a failed suggestion is logged and never fatal -- other profiles apply.
void Suggest(const char* profile, const XrActionSuggestedBinding* bindings,
             uint32_t count) {
  XrPath profile_path = XR_NULL_PATH;
  if (!MakePath(profile, profile_path)) return;
  XrInteractionProfileSuggestedBinding suggested{
      XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
  suggested.interactionProfile = profile_path;
  suggested.suggestedBindings = bindings;
  suggested.countSuggestedBindings = count;
  OkInput(xrSuggestInteractionProfileBindings(g_instance, &suggested),
          profile);
}

void SuggestAll() {
  // Build with resolved paths (xrStringToPath needs the literal strings).
  struct Row {
    XrAction action;
    const char* left;
    const char* right;
  };
  const Row full[] = {
      {g_grip_pose, "/user/hand/left/input/grip/pose",
       "/user/hand/right/input/grip/pose"},
      {g_aim_pose, "/user/hand/left/input/aim/pose",
       "/user/hand/right/input/aim/pose"},
      {g_squeeze, "/user/hand/left/input/squeeze/click",
       "/user/hand/right/input/squeeze/click"},
      {g_trigger, "/user/hand/left/input/trigger/value",
       "/user/hand/right/input/trigger/value"},
      {g_stick, "/user/hand/left/input/thumbstick",
       "/user/hand/right/input/thumbstick"},
      {g_haptic, "/user/hand/left/output/haptic",
       "/user/hand/right/output/haptic"},
      {g_menu, "/user/hand/left/input/menu/click",
       "/user/hand/right/input/menu/click"},
      {g_stick_click, "/user/hand/left/input/thumbstick/click",
       "/user/hand/right/input/thumbstick/click"},
  };
  const char* profiles[] = {
      "/interaction_profiles/microsoft/motion_controller",
  };
  for (const char* profile : profiles) {
    XrActionSuggestedBinding list[16]{};
    uint32_t n = 0;
    for (const Row& row : full) {
      for (int side = 0; side < 2; ++side) {
        XrPath p = XR_NULL_PATH;
        if (!MakePath(side == 0 ? row.left : row.right, p)) return;
        list[n].action = row.action;
        list[n].binding = p;
        ++n;
      }
    }
    Suggest(profile, list, n);
  }
  // Face/menu/click bindings vary by profile. Quest Touch is the user's
  // current target; Index keeps its A/B + stick clicks, and WMR keeps the
  // controls that profile exposes. Unsupported suggestions fail soft.
  {
    XrActionSuggestedBinding list[19]{};
    uint32_t n = 0;
    const struct { XrAction action; const char* path; } rows[] = {
        {g_grip_pose, "/user/hand/left/input/grip/pose"},
        {g_grip_pose, "/user/hand/right/input/grip/pose"},
        {g_aim_pose, "/user/hand/left/input/aim/pose"},
        {g_aim_pose, "/user/hand/right/input/aim/pose"},
        {g_squeeze, "/user/hand/left/input/squeeze/value"},
        {g_squeeze, "/user/hand/right/input/squeeze/value"},
        {g_trigger, "/user/hand/left/input/trigger/value"},
        {g_trigger, "/user/hand/right/input/trigger/value"},
        {g_stick, "/user/hand/left/input/thumbstick"},
        {g_stick, "/user/hand/right/input/thumbstick"},
        {g_haptic, "/user/hand/left/output/haptic"},
        {g_haptic, "/user/hand/right/output/haptic"},
        {g_primary, "/user/hand/left/input/x/click"},
        {g_primary, "/user/hand/right/input/a/click"},
        {g_secondary, "/user/hand/left/input/y/click"},
        {g_secondary, "/user/hand/right/input/b/click"},
        {g_menu, "/user/hand/left/input/menu/click"},
        {g_stick_click, "/user/hand/left/input/thumbstick/click"},
        {g_stick_click, "/user/hand/right/input/thumbstick/click"},
    };
    for (const auto& row : rows) {
      XrPath p = XR_NULL_PATH;
      if (!MakePath(row.path, p)) return;
      list[n++] = {row.action, p};
    }
    Suggest("/interaction_profiles/oculus/touch_controller", list, n);
  }
  {
    XrActionSuggestedBinding list[20]{};
    uint32_t n = 0;
    const struct { XrAction action; const char* path; } rows[] = {
        {g_grip_pose, "/user/hand/left/input/grip/pose"},
        {g_grip_pose, "/user/hand/right/input/grip/pose"},
        {g_aim_pose, "/user/hand/left/input/aim/pose"},
        {g_aim_pose, "/user/hand/right/input/aim/pose"},
        {g_squeeze, "/user/hand/left/input/squeeze/value"},
        {g_squeeze, "/user/hand/right/input/squeeze/value"},
        {g_trigger, "/user/hand/left/input/trigger/value"},
        {g_trigger, "/user/hand/right/input/trigger/value"},
        {g_stick, "/user/hand/left/input/thumbstick"},
        {g_stick, "/user/hand/right/input/thumbstick"},
        {g_haptic, "/user/hand/left/output/haptic"},
        {g_haptic, "/user/hand/right/output/haptic"},
        {g_primary, "/user/hand/left/input/a/click"},
        {g_primary, "/user/hand/right/input/a/click"},
        {g_secondary, "/user/hand/left/input/b/click"},
        {g_secondary, "/user/hand/right/input/b/click"},
        {g_stick_click, "/user/hand/left/input/thumbstick/click"},
        {g_stick_click, "/user/hand/right/input/thumbstick/click"},
        {g_menu, "/user/hand/left/input/system/click"},
        {g_menu, "/user/hand/right/input/system/click"},
    };
    for (const auto& row : rows) {
      XrPath p = XR_NULL_PATH;
      if (!MakePath(row.path, p)) return;
      list[n++] = {row.action, p};
    }
    Suggest("/interaction_profiles/valve/index_controller", list, n);
  }
  // Submit each profile once: a later successful suggestion replaces the
  // entire previous set, including poses, analog input and haptics.
  // KHR simple: poses + haptics only (no stick/squeeze/trigger values).
  {
    XrActionSuggestedBinding list[6]{};
    uint32_t n = 0;
    const Row poses[] = {
        {g_grip_pose, "/user/hand/left/input/grip/pose",
         "/user/hand/right/input/grip/pose"},
        {g_aim_pose, "/user/hand/left/input/aim/pose",
         "/user/hand/right/input/aim/pose"},
        {g_haptic, "/user/hand/left/output/haptic",
         "/user/hand/right/output/haptic"},
    };
    for (const Row& row : poses) {
      for (int side = 0; side < 2; ++side) {
        XrPath p = XR_NULL_PATH;
        if (!MakePath(side == 0 ? row.left : row.right, p)) return;
        list[n].action = row.action;
        list[n].binding = p;
        ++n;
      }
    }
    Suggest("/interaction_profiles/khr/simple_controller", list, n);
  }
}

float ReadFloat(XrAction action, XrPath subpath, bool& active) {
  XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
  get.action = action;
  get.subactionPath = subpath;
  XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};
  active = false;
  if (XR_FAILED(xrGetActionStateFloat(g_session, &get, &state)))
    return 0.0f;
  active = state.isActive != XR_FALSE;
  return active ? state.currentState : 0.0f;
}

bool ReadBool(XrAction action, XrPath subpath, bool& active) {
  XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
  get.action = action;
  get.subactionPath = subpath;
  XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
  active = false;
  if (XR_FAILED(xrGetActionStateBoolean(g_session, &get, &state)))
    return false;
  active = state.isActive != XR_FALSE;
  return active && state.currentState != XR_FALSE;
}

XrVector2f ReadStick(XrPath subpath, bool& active, bool* query_failed = nullptr) {
  XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
  get.action = g_stick;
  get.subactionPath = subpath;
  XrActionStateVector2f state{XR_TYPE_ACTION_STATE_VECTOR2F};
  active = false;
  const XrResult result = xrGetActionStateVector2f(g_session, &get, &state);
  if (query_failed) *query_failed = XR_FAILED(result);
  if (XR_FAILED(result))
    return {};
  active = state.isActive != XR_FALSE;
  return active ? state.currentState : XrVector2f{};
}

bool ReadHeadOrientation(XrSpace base_space, XrTime time, float quat[4]) {
  if (!g_view_space) return false;
  XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
  if (XR_FAILED(xrLocateSpace(g_view_space, base_space, time, &location)) ||
      !(location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))
    return false;
  quat[0] = location.pose.orientation.x;
  quat[1] = location.pose.orientation.y;
  quat[2] = location.pose.orientation.z;
  quat[3] = location.pose.orientation.w;
  float norm2 = 0.0f;
  for (float value : {quat[0], quat[1], quat[2], quat[3]}) {
    if (!std::isfinite(value)) return false;
    norm2 += value * value;
  }
  return norm2 > 0.5f && norm2 < 1.5f;
}

void PublishPad(const VirtualPadState& pad, bool active) {
  std::lock_guard<std::mutex> lock(g_pad_mutex);
  g_pad_state = pad;
  g_pad_active = active;
}

void PublishArmTargets(const ArmTarget targets[2], const HandSample hands[2]) {
  std::lock_guard<std::mutex> lock(g_arm_mutex);
  g_arm_target[0] = targets[0];
  g_arm_target[1] = targets[1];
  g_hand[0] = hands[0];
  g_hand[1] = hands[1];
}

void SyncParams(const Config& cfg) {
  TurnParams tp;
  tp.snap_enabled = cfg.snap_turn;
  tp.smooth_enabled = cfg.smooth_turn;
  tp.snap_degrees = (float)cfg.snap_turn_degrees;
  tp.smooth_speed_dps = cfg.smooth_turn_speed_dps;
  if (!g_turn_params_init || tp.snap_enabled != g_turn_params.snap_enabled ||
      tp.smooth_enabled != g_turn_params.smooth_enabled ||
      tp.snap_degrees != g_turn_params.snap_degrees ||
      tp.smooth_speed_dps != g_turn_params.smooth_speed_dps) {
    g_turn = TurnTracker(tp);
    g_turn_params = tp;
    g_turn_params_init = true;
  }
}

}  // namespace



namespace {
struct RoomscaleSampler {
  Vec3 previous_head{};
  long long previous_time_ns = 0;
  std::uint64_t epoch = 0;
  std::uint64_t calibration_revision = 0;
  float world_scale = 1.f;
  bool primed = false;
};
std::mutex g_roomscale_mutex;
RoomscaleBasis g_roomscale_basis{};
RoomscaleMotion g_roomscale_motion{};
RoomscaleSampler g_roomscale_sampler{};

void ResetRoomscaleLocked() {
  g_roomscale_motion = {};
  g_roomscale_sampler = {};
}

void ResetRoomscaleState() {
  std::lock_guard<std::mutex> lock(g_roomscale_mutex);
  ResetRoomscaleLocked();
}


bool UpdateRoomscaleInput(VrPadInput& pad, Vec3 head, std::uint64_t epoch,
                          long long predicted_time_ns, const Config& cfg,
                          bool sanctuary) {
  const bool allowed = cfg.vr_enabled && cfg.headlook_enabled &&
      cfg.play_style == "roomscale" && !sanctuary && !ModMenuCapturesInput() &&
      !CalibrationCapturesInput() && !g_capture_rearm && epoch &&
      predicted_time_ns > 0 && FiniteVec(head);
  const auto now = GetTickCount64();
  std::lock_guard<std::mutex> lock(g_roomscale_mutex);
  if (!allowed) { ResetRoomscaleLocked(); return false; }
  const auto& basis = g_roomscale_basis;
  if (!basis.valid || basis.space_epoch != epoch || now < basis.published_ms ||
      now - basis.published_ms > 100) { ResetRoomscaleLocked(); return false; }
  if (!g_roomscale_sampler.primed || g_roomscale_sampler.epoch != epoch ||
       g_roomscale_sampler.calibration_revision != basis.calibration_revision ||
       g_roomscale_sampler.world_scale != basis.world_scale ||
      predicted_time_ns <= g_roomscale_sampler.previous_time_ns ||
      predicted_time_ns - g_roomscale_sampler.previous_time_ns > 100000000) {
    g_roomscale_sampler.previous_head = head;
    g_roomscale_sampler.previous_time_ns = predicted_time_ns;
    g_roomscale_sampler.epoch = epoch;
    g_roomscale_sampler.calibration_revision = basis.calibration_revision;
    g_roomscale_sampler.world_scale = basis.world_scale;
    g_roomscale_sampler.primed = true;
    g_roomscale_motion = {};
    return false;
  }
  const float dt = static_cast<float>(predicted_time_ns -
      g_roomscale_sampler.previous_time_ns) * 1e-9f;
  const Vec3 delta{head.x - g_roomscale_sampler.previous_head.x,
                   head.y - g_roomscale_sampler.previous_head.y,
                   head.z - g_roomscale_sampler.previous_head.z};
  g_roomscale_sampler.previous_head = head;
  g_roomscale_sampler.previous_time_ns = predicted_time_ns;
  if (!(dt > 0.0f) || !std::isfinite(dt)) { ResetRoomscaleLocked(); return false; }
  const Vec3 world_delta = VAdd(VAdd(VScale(basis.tracking_right, delta.x),
      VScale(basis.tracking_up, delta.y)), VScale(basis.tracking_forward, -delta.z));
  const Vec3 world_velocity = VScale(world_delta, 1.0f / dt);
  VrHeading native;
  if (!FiniteVec(world_velocity) || !native.SeedOnce(basis.movement_basis)) {
    g_roomscale_motion = {};
    return false;
  }
  const Basis movement = native.BodyBasis();
  const float right_speed = VDot(world_velocity, movement.r);
  const float forward_speed = VDot(world_velocity, movement.f);
  const RoomscaleAxes axes = RoomscaleAxesFromVelocity(
      right_speed, forward_speed, basis.world_scale);
  pad.roomscale_x = axes.x;
  pad.roomscale_y = axes.y;
  if (axes.active) {
    g_roomscale_motion.world_velocity = VAdd(
        VScale(movement.r, right_speed * basis.world_scale),
        VScale(movement.f, forward_speed * basis.world_scale));
    g_roomscale_motion.space_epoch = epoch;
    g_roomscale_motion.published_ms = now;
    g_roomscale_motion.expires_ms = now + 150;
    g_roomscale_motion.active = true;
    return true;
  }
  if (!g_roomscale_motion.active || now > g_roomscale_motion.expires_ms ||
      g_roomscale_motion.space_epoch != epoch) g_roomscale_motion = {};
  return false;
}


}  // namespace
void InputAttachSession(void* xr_instance, void* xr_session) {
  if (g_attached || g_failed) return;
  Config cfg = DiagnosticsCurrentConfig();
  if (!cfg.xr_input_enabled) {
    if (!g_logged_unavailable) {
      g_logged_unavailable = true;
      DSVR_LOG("Input: controller actions disabled (xr_input_enabled=false; "
               "enable in [controls] for the P4 test, then restart)");
    }
    return;
  }
  g_instance = (XrInstance)xr_instance;
  g_session = (XrSession)xr_session;
  if (!g_instance || !g_session) return;

  XrReferenceSpaceCreateInfo view_info{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
  view_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
  view_info.poseInReferenceSpace.orientation.w = 1.0f;
  if (XR_FAILED(xrCreateReferenceSpace(g_session, &view_info, &g_view_space))) {
    g_view_space = XR_NULL_HANDLE;
    DSVR_WARN("Input: view-space orientation unavailable; hand movement references fall back to head");
  }

  XrActionSetCreateInfo set_info{XR_TYPE_ACTION_SET_CREATE_INFO};
  strncpy_s(set_info.actionSetName, "gameplay", _TRUNCATE);
  strncpy_s(set_info.localizedActionSetName, "DSVR gameplay", _TRUNCATE);
  set_info.priority = 0;
  if (!OkInput(xrCreateActionSet(g_instance, &set_info, &g_action_set),
               "xrCreateActionSet")) {
    g_failed = true;
    return;
  }
  if (!MakePath("/user/hand/left", g_hand_path[0]) ||
      !MakePath("/user/hand/right", g_hand_path[1]) ||
      !MakeAction(XR_ACTION_TYPE_POSE_INPUT, "grip_pose", "Grip pose", true,
                  g_grip_pose) ||
      !MakeAction(XR_ACTION_TYPE_POSE_INPUT, "aim_pose", "Pointer aim", true,
                  g_aim_pose) ||
      !MakeAction(XR_ACTION_TYPE_FLOAT_INPUT, "squeeze", "Squeeze", true,
                  g_squeeze) ||
      !MakeAction(XR_ACTION_TYPE_FLOAT_INPUT, "trigger", "Trigger", true,
                  g_trigger) ||
      !MakeAction(XR_ACTION_TYPE_VECTOR2F_INPUT, "thumbstick", "Thumbstick",
                  true, g_stick) ||
      !MakeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "primary", "Primary button",
                  true, g_primary) ||
      !MakeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "secondary", "Secondary button",
                  true, g_secondary) ||
      !MakeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "menu", "Menu button", true,
                  g_menu) ||
      !MakeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "stick_click", "Stick click",
                  true, g_stick_click) ||
      !MakeAction(XR_ACTION_TYPE_VIBRATION_OUTPUT, "haptic", "Haptic", true,
                  g_haptic)) {
    g_failed = true;
    return;
  }
  SuggestAll();
  XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
  attach.countActionSets = 1;
  attach.actionSets = &g_action_set;
  if (!OkInput(xrAttachSessionActionSets(g_session, &attach),
               "xrAttachSessionActionSets")) {
    g_failed = true;
    return;
  }
  for (int side = 0; side < 2; ++side) {
    XrActionSpaceCreateInfo space_info{XR_TYPE_ACTION_SPACE_CREATE_INFO};
    space_info.action = g_grip_pose;
    space_info.subactionPath = g_hand_path[side];
    space_info.poseInActionSpace.orientation.w = 1.0f;
    if (!OkInput(xrCreateActionSpace(g_session, &space_info,
                                     &g_grip_space[side]),
                 "xrCreateActionSpace(grip)")) {
      g_failed = true;
      return;
    }
    space_info.action = g_aim_pose;
    if (XR_FAILED(xrCreateActionSpace(g_session, &space_info, &g_aim_space[side]))) {
      g_aim_space[side]=XR_NULL_HANDLE;
      DSVR_WARN("Input: aim space unavailable; controller navigation remains available");
    }
  }
  SyncParams(cfg);
  InputInvalidateHeadDirectedLocomotion();
  g_attached = true;
  DSVR_LOG("Input: OpenXR actions attached (Touch/Index/WMR/simple "
           "bindings suggested). Continuous hands for IK arms; no swing "
           "triggers, no game writes.");
}

bool InputAvailable() { return g_attached && !g_failed; }

void InputInvalidateFrame() {
  const ArmTarget targets[2]{};
  const HandSample hands[2]{};
  PublishArmTargets(targets, hands);
  PublishPad({}, false);
  InputInvalidateHeadDirectedLocomotion();
  g_pending_turn_yaw.store(0.0f, std::memory_order_relaxed);
  g_combos.Reset();
  g_sanctuary_chord.Reset();
  g_turn.Reset();
  g_blade[0].Reset();
  g_blade[1].Reset();
  g_last_time_ns = 0;
  g_menu_chord_started_ns = 0;
  g_menu_chord_fired = false;
  g_stick_menu_chord_started_ns = 0;
  g_stick_menu_chord_fired = false;
  g_recalibration_first_a_ns = 0;
  g_recalibration_a_count = 0;
  g_recalibration_a_was_down = false;
  g_recalibration_fired = false;

  ResetRoomscaleState();
}

void InputPublishHeadDirectedLocomotion(Vec3 head_forward,
                                       Vec3 movement_basis_forward,
                                       std::uint64_t space_epoch) {
  HeadDirectedLocomotion next{};
  VrHeading heading;
  if (space_epoch && heading.SeedOnce(head_forward) &&
      heading.CandidateLocomotion(0.0f, 0.0f, movement_basis_forward).valid) {
    next.head_forward = head_forward;
    next.movement_basis = movement_basis_forward;
    next.space_epoch = space_epoch;
    next.published_ms = GetTickCount64();
    next.valid = true;
  }
  std::lock_guard<std::mutex> lock(g_locomotion_mutex);
  g_head_directed_locomotion = next;
}

void InputPublishRoomscaleBasis(const RoomscaleBasis& basis) {
  std::lock_guard<std::mutex> lock(g_roomscale_mutex);
  if (!basis.space_epoch || !FiniteVec(basis.tracking_right) ||
      !std::isfinite(basis.world_scale) || !(basis.world_scale > 0.f) ||
      !FiniteVec(basis.tracking_up) || !FiniteVec(basis.tracking_forward) ||
      !FiniteVec(basis.movement_basis) || VLen(basis.tracking_right) < 0.9f ||
      VLen(basis.tracking_up) < 0.9f || VLen(basis.tracking_forward) < 0.9f ||
      VLen(basis.movement_basis) < 0.5f) {
    g_roomscale_basis = {};
    ResetRoomscaleLocked();
    return;
  }
  g_roomscale_basis = basis;
  g_roomscale_basis.published_ms = GetTickCount64();
  g_roomscale_basis.valid = true;
  if (g_roomscale_sampler.epoch != basis.space_epoch ||
      g_roomscale_sampler.calibration_revision != basis.calibration_revision ||
      g_roomscale_sampler.world_scale != basis.world_scale) ResetRoomscaleLocked();
}

bool InputLatestRoomscaleMotion(RoomscaleMotion& out) {
  out = {};
  std::lock_guard<std::mutex> lock(g_roomscale_mutex);
  const auto now = GetTickCount64();
  if (!g_roomscale_basis.valid || !g_roomscale_motion.active ||
      g_roomscale_motion.space_epoch != g_roomscale_basis.space_epoch ||
      now < g_roomscale_motion.published_ms ||
      now > g_roomscale_motion.expires_ms) return false;
  out = g_roomscale_motion;
  return true;
}


void InputInvalidateRoomscaleBasis() {
  std::lock_guard<std::mutex> lock(g_roomscale_mutex);
  g_roomscale_basis = {};
  ResetRoomscaleLocked();
}

void InputInvalidateHeadDirectedLocomotion() {
  std::lock_guard<std::mutex> lock(g_locomotion_mutex);
  g_head_directed_locomotion = {};

  InputInvalidateRoomscaleBasis();
}

void InputCopyTrackingSamples(HandSample hands[2], ArmTarget arms[2]) {
  std::lock_guard<std::mutex> lock(g_arm_mutex);
  for (int side = 0; side < 2; ++side) {
    hands[side] = g_hand[side];
    arms[side] = g_arm_target[side];
  }
}

void InputPollFrame(void* base_space, const float head_pos[3],
                    std::uint64_t space_epoch,
                    long long predicted_display_time) {
  if (!InputAvailable() || !base_space || !head_pos ||
      predicted_display_time <= 0) {
    InputInvalidateFrame();
    return;
  }
  const Vec3 head{head_pos[0], head_pos[1], head_pos[2]};
  if (!FiniteVec(head)) {
    InputInvalidateFrame();
    return;
  }
  Config cfg = DiagnosticsCurrentConfig();
  SyncParams(cfg);

  XrActiveActionSet active{g_action_set, XR_NULL_PATH};
  XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
  sync.countActiveActionSets = 1;
  sync.activeActionSets = &active;
  XrResult sync_result = xrSyncActions(g_session, &sync);
  if (sync_result == XR_SESSION_NOT_FOCUSED) {
    InputInvalidateFrame();
    return;  // runtime/OS focus: never leave a stale button held
  }
  if (!OkInput(sync_result, "xrSyncActions")) {
    InputInvalidateFrame();
    g_failed = true;
    return;
  }

  float dt = 0.0f;
  if (g_last_time_ns > 0)
    dt = (float)(predicted_display_time - g_last_time_ns) * 1e-9f;
  g_last_time_ns = predicted_display_time;

  const bool sanctuary = XrMenuPresentationActive();
  const HandParams hand_params;
  VrPadInput pad_input{};
  bool pad_active = false;
  bool right_stick_active = false;
  bool right_stick_failed = false;
  ArmTarget arm_targets[2]{};
  HandSample hand_samples[2]{};

  for (int side = 0; side < 2; ++side) {
    // Non-pose actions remain usable when grip tracking is unavailable.
    VrPadHand& pad_hand = side == 0 ? pad_input.left : pad_input.right;
    bool action_active = false;
    const float squeeze_raw = ReadFloat(g_squeeze, g_hand_path[side], action_active);
    pad_active = pad_active || action_active;
    const float trigger_raw = ReadFloat(g_trigger, g_hand_path[side], action_active);
    pad_active = pad_active || action_active;
    XrVector2f stick = ReadStick(g_hand_path[side], action_active,
                                side == 1 ? &right_stick_failed : nullptr);
    if (side == 1) right_stick_active = action_active;
    pad_active = pad_active || action_active;
    pad_hand.stick_x = stick.x;
    pad_hand.stick_y = stick.y;
    pad_hand.squeeze = squeeze_raw;
    pad_hand.trigger = trigger_raw;
    pad_hand.primary = ReadBool(g_primary, g_hand_path[side], action_active);
    pad_active = pad_active || action_active;
    pad_hand.secondary = ReadBool(g_secondary, g_hand_path[side], action_active);
    pad_active = pad_active || action_active;
    pad_hand.menu = ReadBool(g_menu, g_hand_path[side], action_active);
    pad_active = pad_active || action_active;
    pad_hand.stick_click = ReadBool(g_stick_click, g_hand_path[side], action_active);
    pad_active = pad_active || action_active;
    HandSample& sample = hand_samples[side];
    sample.squeeze_held = squeeze_raw > 0.7f;
    sample.trigger_held = trigger_raw > 0.7f;
    sample.primary_held = pad_hand.primary;
    // Aim is located separately in the exact same space/time as grip and HMD.
    // Grip tracking loss cannot disable navigation buttons or a valid aim.
    XrActionStateGetInfo aim_get{XR_TYPE_ACTION_STATE_GET_INFO};
    aim_get.action=g_aim_pose;aim_get.subactionPath=g_hand_path[side];
    XrActionStatePose aim_state{XR_TYPE_ACTION_STATE_POSE};
    XrSpaceLocation aim{XR_TYPE_SPACE_LOCATION};
    constexpr auto aim_flags=XR_SPACE_LOCATION_ORIENTATION_VALID_BIT |
                              XR_SPACE_LOCATION_POSITION_VALID_BIT;
    if (g_aim_space[side] &&
        XR_SUCCEEDED(xrGetActionStatePose(g_session,&aim_get,&aim_state)) && aim_state.isActive &&
        XR_SUCCEEDED(xrLocateSpace(g_aim_space[side],(XrSpace)base_space,
                                   (XrTime)predicted_display_time,&aim)) &&
        (aim.locationFlags & aim_flags)==aim_flags) {
      sample.aim_pos[0]=aim.pose.position.x;sample.aim_pos[1]=aim.pose.position.y;
      sample.aim_pos[2]=aim.pose.position.z;
      sample.aim_quat[0]=aim.pose.orientation.x;sample.aim_quat[1]=aim.pose.orientation.y;
      sample.aim_quat[2]=aim.pose.orientation.z;sample.aim_quat[3]=aim.pose.orientation.w;
      float norm2=0;for(float v:sample.aim_quat)norm2+=v*v;
      sample.aim_valid=FiniteVec({sample.aim_pos[0],sample.aim_pos[1],sample.aim_pos[2]}) &&
          std::isfinite(norm2) && norm2>.5f && norm2<1.5f;
    }
    XrSpaceVelocity velocity{XR_TYPE_SPACE_VELOCITY};
    XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
    location.next = &velocity;
    XrResult located = xrLocateSpace(g_grip_space[side], (XrSpace)base_space,
                                     (XrTime)predicted_display_time, &location);
    const auto kValid = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT |
                        XR_SPACE_LOCATION_POSITION_VALID_BIT;
    if (XR_FAILED(located) || (location.locationFlags & kValid) != kValid) {
      sample.valid = false;
      g_blade[side].Reset();
      continue;
    }
    sample.valid = true;
    sample.pos[0] = location.pose.position.x;
    sample.pos[1] = location.pose.position.y;
    sample.pos[2] = location.pose.position.z;
    sample.quat[0] = location.pose.orientation.x;
    sample.quat[1] = location.pose.orientation.y;
    sample.quat[2] = location.pose.orientation.z;
    sample.quat[3] = location.pose.orientation.w;
    Vec3 blade = BladeDirection(sample.quat[0], sample.quat[1],
                                sample.quat[2], sample.quat[3]);
    sample.blade_dir[0] = blade.x;
    sample.blade_dir[1] = blade.y;
    sample.blade_dir[2] = blade.z;
    Vec3 grip{sample.pos[0], sample.pos[1], sample.pos[2]};
    Vec3 tip = BladeTip(grip, sample.quat[0], sample.quat[1], sample.quat[2],
                        sample.quat[3], hand_params.blade_length_m);
    sample.tip[0] = tip.x;
    sample.tip[1] = tip.y;
    sample.tip[2] = tip.z;
    sample.xr_speed = -1.0f;
    if (velocity.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) {
      Vec3 v{velocity.linearVelocity.x, velocity.linearVelocity.y,
             velocity.linearVelocity.z};
      sample.xr_speed = VLen(v);
    }
    sample.squeeze_held = squeeze_raw > 0.7f;
    sample.trigger_held = trigger_raw > 0.7f;
    sample.primary_held = pad_hand.primary;

    // Continuous blade kinematics: the blade IS the hand. No swing
    // classifier, no trigger, no pulse. Sanctuary freezes the blade so
    // menu hands never damage; motion_attacks=false freezes likewise.
    // Velocities are head-relative: roomscale walking moves head and
    // hands together and must not read as a swing.
    if (sanctuary || ModMenuCapturesInput() || CalibrationCapturesInput() ||
        g_capture_rearm || !cfg.motion_attacks) {
      g_blade[side].Reset();
      sample.speed = 0.0f;
    } else {
      g_blade[side].FeedRelative(grip, tip, head, dt, space_epoch);
      sample.speed = g_blade[side].grip_speed;
    }
    if (!g_hand_logged[side]) {
      g_hand_logged[side] = true;
      DSVR_LOG("Input: %s hand tracking valid (grip=%.2f,%.2f,%.2f "
               "blade=%.2f,%.2f,%.2f) -- continuous IK, no triggers",
               side == 0 ? "left" : "right", sample.pos[0], sample.pos[1],
               sample.pos[2], blade.x, blade.y, blade.z);
    }

    ArmTarget& target = arm_targets[side];
    target.valid = sample.valid;
    target.pos[0] = sample.pos[0];
    target.pos[1] = sample.pos[1];
    target.pos[2] = sample.pos[2];
    target.quat[0] = sample.quat[0];
    target.quat[1] = sample.quat[1];
    target.quat[2] = sample.quat[2];
    target.quat[3] = sample.quat[3];
    target.blade_dir[0] = sample.blade_dir[0];
    target.blade_dir[1] = sample.blade_dir[1];
    target.blade_dir[2] = sample.blade_dir[2];
    target.tip[0] = sample.tip[0];
    target.tip[1] = sample.tip[1];
    target.tip[2] = sample.tip[2];
    target.tip_speed = g_blade[side].tip_speed;
    target.grip_speed = g_blade[side].grip_speed;
    target.frame_kind = g_blade[side].FrameKind(blade);
    target.squeeze_held = sample.squeeze_held;
    target.trigger_held = sample.trigger_held;
  }

  PublishArmTargets(arm_targets, hand_samples);

  // Both stick clicks are reserved while held. A short pair keeps the legacy
  // recenter action; a three-second hold opens Home once and requires neutral.
  const bool stick_menu_pair = pad_input.left.stick_click && pad_input.right.stick_click;
  bool stick_menu_consumed = false;
  if (stick_menu_pair) {
    if (!g_stick_menu_chord_started_ns ||
        predicted_display_time < g_stick_menu_chord_started_ns)
      g_stick_menu_chord_started_ns = predicted_display_time;
    g_capture_rearm = true;
    stick_menu_consumed = true;
    if (!g_stick_menu_chord_fired &&
        predicted_display_time - g_stick_menu_chord_started_ns >= kStickMenuChordNs) {
      ModMenuToggle();
      g_stick_menu_chord_fired = true;
    }
  } else if (g_stick_menu_chord_started_ns) { if (!g_stick_menu_chord_fired) XrRecenterSanctuary(); g_stick_menu_chord_started_ns = 0; g_capture_rearm = true; stick_menu_consumed = true; }

  const bool a_pressed = pad_input.right.primary && !g_recalibration_a_was_down;
  g_recalibration_a_was_down = pad_input.right.primary;
  bool recalibration_consumed = false;
  if (stick_menu_pair) {
        g_recalibration_first_a_ns = 0;
        g_recalibration_a_count = 0;
      } else if (pad_input.left.stick_click) {
        if (g_recalibration_a_count &&
            (predicted_display_time < g_recalibration_first_a_ns ||
             predicted_display_time - g_recalibration_first_a_ns > kDoubleAClickWindowNs)) {
          g_recalibration_first_a_ns = 0;
          g_recalibration_a_count = 0;
        }
        if (a_pressed && !g_recalibration_fired) {
          if (g_recalibration_a_count == 1) {
            if (predicted_display_time - g_recalibration_first_a_ns >=
                  kDoubleAClickMinSpacingNs) {
                CalibrationRequest();
                g_recalibration_fired = true;
                g_recalibration_a_count = 0;
            }
          } else {
              g_recalibration_first_a_ns = predicted_display_time;
              g_recalibration_a_count = 1;
            }
          recalibration_consumed = true;
          g_capture_rearm = true;
        }
        recalibration_consumed = recalibration_consumed ||
            g_recalibration_a_count != 0 || g_recalibration_fired;
      } else {
        g_recalibration_first_a_ns = 0;
        g_recalibration_a_count = 0;
      }
  if (recalibration_consumed) g_capture_rearm = true;

  // Home in the headset: hold left Menu + right grip for half a second.
  // Reserve both controls while pending, and toggle once until released.
  const bool menu_chord = pad_input.left.menu && pad_input.right.squeeze > 0.7f;
  if (menu_chord) {
    if (!g_menu_chord_started_ns || predicted_display_time < g_menu_chord_started_ns)
      g_menu_chord_started_ns = predicted_display_time;
    if (!g_menu_chord_fired &&
        predicted_display_time - g_menu_chord_started_ns >= 500000000) {
      ModMenuToggle();
      g_menu_chord_fired = true;
    }
  } else {
    g_menu_chord_started_ns = 0;
    g_menu_chord_fired = false;
  }
  const bool capture = ModMenuCapturesInput() || CalibrationCapturesInput();
  if (capture && !stick_menu_consumed && !recalibration_consumed)
    ModMenuControllerInput(pad_input.left.stick_x, pad_input.left.stick_y,
                           pad_input.right.primary ||
                               (!CalibrationCapturesInput() && pad_input.right.trigger > 0.7f),
                           pad_input.right.secondary);
  if (capture) g_capture_rearm = true;
  // The trigger which completes calibration, or a held menu selection, must
  // be released before it becomes a gameplay action. Hands still publish.
  const auto neutral = [](const VrPadHand& hand) {
    return std::fabs(hand.stick_x) < 0.2f && std::fabs(hand.stick_y) < 0.2f &&
        hand.squeeze < 0.2f && hand.trigger < 0.2f && !hand.primary &&
        !hand.secondary && !hand.menu && !hand.stick_click;
  };
  if (!capture && neutral(pad_input.left) && neutral(pad_input.right)) {
      g_capture_rearm = false;
      g_stick_menu_chord_fired = false;
      g_recalibration_fired = false;
    }
  if (capture || g_capture_rearm || menu_chord || stick_menu_consumed ||
      recalibration_consumed) {
    PublishPad({}, pad_active);
    g_turn.Reset();
    g_combos.Reset();
    g_sanctuary_chord.Reset();
    g_pending_turn_yaw.store(0.0f, std::memory_order_relaxed);
    g_blade[0].Reset();
    g_blade[1].Reset();
    return;
  }

  // Controller chords for the F-key functions (combo_math.h). While Menu
  // is held, the chord face buttons are consumed here, not forwarded to
  // the game as A/B/X/Y presses.
  {
    ComboButtons buttons{};
    buttons.menu = pad_input.left.menu;
    buttons.x = pad_input.left.primary;
    buttons.y = pad_input.left.secondary;
    buttons.b = pad_input.right.secondary;
    buttons.lstick = pad_input.left.stick_click;
    buttons.rstick = pad_input.right.stick_click;
    const unsigned combos = g_combos.Update(buttons);
    // Alternate theater shortcut when Menu belongs to system UI. The same
    // state owns deferred lone taps and suppresses the entire consumed chord.
    const auto secondary = g_sanctuary_chord.Update(
        pad_input.left.secondary, pad_input.right.secondary, predicted_display_time);
    pad_input.left.secondary = secondary.first;
    pad_input.right.secondary = secondary.second;
    if (secondary.triggered) {
      XrToggleSanctuary();
      DSVR_LOG("Input: chord Y+B held -> theater toggle (F8)");
    }
    if (combos & kComboTheater) {
      XrToggleSanctuary();
      DSVR_LOG("Input: combo Menu+Y -> theater toggle (F8)");
    }
    if (combos & kComboRecenter) {
      XrRecenterSanctuary();
      DSVR_LOG("Input: combo both stick clicks -> recenter (F6)");
    }
    if (combos & kComboMarker) {
      DiagnosticsLogMarker();
      DSVR_LOG("Input: combo Menu+X -> log marker (F9)");
    }
    if (combos & kComboReload) {
      DiagnosticsReloadConfig();
      DSVR_LOG("Input: combo Menu+B -> reload config (F7)");
    }
    if (buttons.menu) {
      g_sanctuary_chord.Reset();
      pad_input.left.primary = false;
      pad_input.left.secondary = false;
      pad_input.right.secondary = false;
    }
  }

  pad_input.dominant_left = cfg.dominant_hand_left;
  const bool left_stick_neutral =
      std::hypot(pad_input.left.stick_x, pad_input.left.stick_y) <= 0.15f;
  if (!g_movement_settings_initialized || left_stick_neutral) {
    const auto next_orientation = cfg.movement_orientation;
    const int next_hand = cfg.movement_controller == MovementController::Left ? 0 :
        cfg.movement_controller == MovementController::Right ? 1 :
        cfg.dominant_hand_left ? 1 : 0;
    if (!g_movement_settings_initialized ||
        next_orientation != g_active_movement_orientation ||
        next_hand != g_active_movement_hand) {
      g_have_controller_heading = false;
      g_controller_vertical = false;
      g_controller_rearm_required = false;
    }
    g_active_movement_orientation = next_orientation;
    g_active_movement_hand = next_hand;
    g_movement_settings_initialized = true;
  }
  // Visual IK is not a replacement for controls. Keep normal combat buttons
  // until real native physical damage AND defense are bound; enabling IK or
  // log-only must never leave the player unable to attack/block/parry.
  const auto combat=CombatNativeGetStatus();
  pad_input.suppress_weapon_attacks = cfg.arm_ik_enabled && !cfg.arm_ik_log_only &&
      combat.physical_damage_bound && combat.shield_contact_bound;
  if (!sanctuary) {
    HeadDirectedLocomotion locomotion{};
    {
      std::lock_guard<std::mutex> lock(g_locomotion_mutex);
      locomotion = g_head_directed_locomotion;
    }
    const auto now = GetTickCount64();
    const bool fresh = locomotion.valid && locomotion.space_epoch == space_epoch &&
        now >= locomotion.published_ms &&
        now - locomotion.published_ms <= kHeadDirectedLocomotionMaxAgeMs;
    if (!fresh) {
      pad_input.left.stick_x = pad_input.left.stick_y = 0.0f;
    } else {
      // The left stick always has exactly one live orientation reference. In
      // Controller mode that is the selected hand's grip heading while it is
      // usable; otherwise the head reference carried by this same fresh
      // locomotion packet steers. A lost or vertically-aimed hand removes
      // hand-relative steering, it never deadens locomotion.
      Vec3 reference_forward = locomotion.head_forward;
      if (g_active_movement_orientation == MovementOrientation::Controller) {
        const int hand = g_active_movement_hand;
        RoomscaleBasis tracking_basis{};
        {
          std::lock_guard<std::mutex> lock(g_roomscale_mutex);
          tracking_basis = g_roomscale_basis;
        }
        const bool tracked = hand_samples[hand].valid && tracking_basis.valid &&
            tracking_basis.space_epoch == space_epoch && now >= tracking_basis.published_ms &&
            now - tracking_basis.published_ms <= kHeadDirectedLocomotionMaxAgeMs;
        if (!tracked) {
          g_have_controller_heading = false;
          g_controller_vertical = false;
          g_controller_rearm_required = true;
          } else {
          if (g_controller_heading_epoch != space_epoch ||
              g_controller_heading_hand != hand) {
            g_have_controller_heading = false;
            g_controller_vertical = false;
            g_controller_rearm_required = true;
            g_controller_heading_epoch = space_epoch;
            g_controller_heading_hand = hand;
          }
          const HandSample& sample = hand_samples[hand];
          const Vec3 tracking_forward = BladeDirection(sample.quat[0], sample.quat[1],
                                                        sample.quat[2], sample.quat[3]);
          const Vec3 world_forward = VAdd(
              VAdd(VScale(tracking_basis.tracking_right, tracking_forward.x),
                   VScale(tracking_basis.tracking_up, tracking_forward.y)),
              VScale(tracking_basis.tracking_forward, -tracking_forward.z));
          const float planar = std::hypot(world_forward.x, world_forward.z);
          if (!FiniteVec(world_forward) || !std::isfinite(planar)) {
            g_have_controller_heading = false;
            g_controller_vertical = false;
            g_controller_rearm_required = true;
            } else {
            if (planar >= 0.3f) {
              g_last_controller_forward =
                  {world_forward.x / planar, 0.0f, world_forward.z / planar};
              g_have_controller_heading = true;
              g_controller_vertical = false;
              if (left_stick_neutral) g_controller_rearm_required = false;
            } else if (planar <= 0.2f) {
              g_controller_vertical = true;
            }
            // The rearm latch gates the HAND heading only: a reacquired hand
              // must not yank a player who is already travelling head-oriented.
            const bool may_reuse = g_have_controller_heading &&
                  (g_controller_vertical || planar >= 0.2f) &&
                !g_controller_rearm_required;
            if (may_reuse) reference_forward = g_last_controller_forward;
          }
        }
      }
      VrHeading heading;
      const auto candidate = heading.SeedOnce(reference_forward)
          ? heading.CandidateLocomotion(pad_input.left.stick_x,
                                        pad_input.left.stick_y,
                                        locomotion.movement_basis)
          : VrLocomotionCandidate{};
      if (candidate.valid) {
        pad_input.left.stick_x = candidate.stick_x;
        pad_input.left.stick_y = candidate.stick_y;
      } else {
        pad_input.left.stick_x = pad_input.left.stick_y = 0.0f;
      }
    }
  }
  { const bool roomscale_active = UpdateRoomscaleInput(pad_input, head, space_epoch, predicted_display_time, cfg, sanctuary); PublishPad(MapVrPad(pad_input,sanctuary||!CameraGameplayReady()), pad_active || roomscale_active); }

  // Stick turn: sanctuary freezes the world; otherwise the tracker owns
  // snap/smooth and only the accumulated delta crosses to the game thread.
  if (sanctuary) {
    g_turn.Reset();
    g_pending_turn_yaw.store(0.0f, std::memory_order_relaxed);
    return;
  }
  if (right_stick_failed) {
    // Query failure invalidates queued turn input, not just this frame's sample.
    g_turn.Reset();
    g_pending_turn_yaw.store(0.0f, std::memory_order_relaxed);
  } else if (right_stick_active) {
    float yaw = g_turn.Feed(pad_input.right.stick_x, dt);
    if (yaw != 0.0f) {
      float old = g_pending_turn_yaw.load(std::memory_order_relaxed);
      while (!g_pending_turn_yaw.compare_exchange_weak(
          old, old + yaw, std::memory_order_relaxed)) {
      }
    }
  }
}

bool InputLatestHand(bool left, HandSample& out) {
  if (!InputAvailable()) return false;
  std::lock_guard<std::mutex> lock(g_arm_mutex);
  const HandSample& sample = g_hand[left ? 0 : 1];
  if (!sample.valid) return false;
  out = sample;
  return true;
}

bool InputLatestArmTarget(bool want_off_hand, ArmTarget& out) {
  if (!InputAvailable()) return false;
  Config cfg = DiagnosticsCurrentConfig();
  const int weapon = WeaponHandIndex(cfg.dominant_hand_left);
  const int off = OffHandIndex(cfg.dominant_hand_left);
  const int side = want_off_hand ? off : weapon;
  std::lock_guard<std::mutex> lock(g_arm_mutex);
  if (!g_arm_target[side].valid) return false;
  out = g_arm_target[side];
  return true;
}

bool InputLatestPad(VirtualPadState& out) {
  std::lock_guard<std::mutex> lock(g_pad_mutex);
  if (!g_pad_active) return false;
  out = g_pad_state;
  return true;
}

float InputTakeTurnYaw() {
  return g_pending_turn_yaw.exchange(0.0f, std::memory_order_relaxed);
}

void InputApplyHaptic(bool left, float amplitude, int duration_ms) {
  if (!InputAvailable() || !(amplitude > 0.0f) || duration_ms <= 0) return;
  if (amplitude > 1.0f) amplitude = 1.0f;
  XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};
  info.action = g_haptic;
  info.subactionPath = g_hand_path[left ? 0 : 1];
  XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};
  vibration.amplitude = amplitude;
  vibration.duration = (XrDuration)duration_ms * 1000000LL;
  vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
  OkInput(xrApplyHapticFeedback(g_session, &info,
                                (XrHapticBaseHeader*)&vibration),
          "xrApplyHapticFeedback");
}

}  // namespace dsvr
