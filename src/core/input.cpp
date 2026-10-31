// Motion input: OpenXR actions (see input.h). Fresh implementation
// against the OpenXR 1.0 core action API; suggested bindings cover Quest
// Touch, Index, WMR motion controllers, and KHR simple (poses only).
// IK design (2026-10-13): poses feed BladeKinematics CONTINUOUSLY
// (finite-difference inside combat_math.h). No swing is classified into
// a trigger and no button pulse fires -- the IK arm is the attack.

#include "input.h"
#include "input_frame_acquisition.h"

#include <windows.h>

#include <openxr/openxr.h>

#include <algorithm>
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
#include "vr_trace.h"
#include "arm_ik.h"
#include "swing_attack_math.h"
#include "shield_guard_math.h"
#include "arm_swing_math.h"
#include "reach_intent_math.h"
#include "interaction_target_math.h"

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
StickDpad g_stick_dpad;
XrAction g_haptic = XR_NULL_HANDLE;
XrPath g_hand_path[2] = {XR_NULL_PATH, XR_NULL_PATH};  // 0 left, 1 right
XrSpace g_grip_space[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
XrSpace g_aim_space[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
XrSpace g_view_space = XR_NULL_HANDLE;  // actual HMD orientation in the input frame
std::atomic<bool> g_attached{false};
std::atomic<bool> g_failed{false};
bool g_logged_unavailable = false;
// A binding edit requests a restart notice on the input thread.
std::atomic<bool> g_binding_reload{false};
// Last reported profile settings, so unchanged edits do not repeat the notice.
struct BindingSignature {
  bool quest3 = true, index = true, wmr = true, simple = true;
};
BindingSignature g_binding_seen{};
bool g_binding_seen_valid = false;
// Profile set the live action set was built with, frozen at session attach.
// g_binding_seen tracks the latest poll for the one-shot warn; this one never
// moves after attach, so applied-vs-attached is a real restart comparison.
BindingSignature g_binding_attached{};
bool g_binding_attached_valid = false;

BladeKinematics g_blade[2];  // per hand, continuous (combat_math.h)
SwingAttackTrigger g_swing_attack[2];  // physical attack bridge, polling thread
ShieldGuardTrigger g_shield_guard[2];  // physical defence bridge, polling thread
ArmSwingSprint g_arm_swing;  // arm-swing gait bridge, polling thread
ReachIntentTrigger g_reach[2];  // reach-to-interact bridge, polling thread
// Like the guard line, the reach tap is a window the caller owns: the trigger
// fires once and the button stays down for reach_press_ms. Measured on the same
// predicted-display clock the trigger itself runs on.
long long g_reach_hold_until_ms[2] = {0, 0};
// Latest native selection published by the interaction observer (game thread).
// No producer binds it yet, so the store stays empty and the reach gate keeps
// refusing; when a producer exists it publishes here and the gate reads the
// live binding instead of a default.
InteractionSelectionStore g_selection_store{};
std::mutex g_selection_mutex;

// A weapon swap, a stance change or a new player actor invalidates every
// physical bridge: an armed swing from the previous weapon must not fire after
// the swap, and a stale reach envelope must not tap the new one. The contact
// bench in arm_ik.cpp already reprimes on the same identity tuple; this is the
// matching reprime on the input side.
CombatNativeStatus g_combat_seen{};
bool g_combat_seen_valid = false;

bool CombatIdentityChanged() {
  const CombatNativeStatus now = CombatNativeGetStatus();
  const bool changed =
      !g_combat_seen_valid || now.equipment_bound != g_combat_seen.equipment_bound ||
      now.equipment.player != g_combat_seen.equipment.player ||
      now.equipment.weapon_id[0] != g_combat_seen.equipment.weapon_id[0] ||
      now.equipment.weapon_id[1] != g_combat_seen.equipment.weapon_id[1] ||
      now.equipment.native_stance != g_combat_seen.equipment.native_stance;
  g_combat_seen = now;
  g_combat_seen_valid = true;
  return changed;
}

void ResetPhysicalBridges() {
  for (int side = 0; side < 2; ++side) {
    g_blade[side].Reset();
    g_swing_attack[side].Reset();
    g_shield_guard[side].Reset();
    g_reach[side].Reset();
    g_reach_hold_until_ms[side] = 0;
  }
  std::lock_guard<std::mutex> lock(g_selection_mutex);
  g_selection_store.Invalidate();
}

// A guard is "the shield hand is up", not a pose: the band is head-relative so
// the block follows the player wherever they hold it, and its width covers an
// overhead guard down to a shield held at the belt. Below the band the hand is
// at rest, not guarding, and the native shield button comes back off.
constexpr float kGuardHandBelowHeadM = 0.85f;
constexpr float kGuardHandAboveHeadM = 0.5f;
ComboDetector g_combos;  // F-key chords (combo_math.h), render thread only
TurnTracker g_turn;
ComfortMotion g_comfort_motion;
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
struct PadTraceState {
  VirtualPadState pad{};
  bool active = false;
  bool initialized = false;
  std::uint64_t capture_id = 0;
  std::uint64_t sequence = 0;
};
PadTraceState g_pad_trace;
struct HandTraceState {
  bool initialized = false;
  bool valid = false;
  std::uint64_t space_epoch = 0;
  std::uint64_t capture_id = 0;
  std::uint64_t sequence = 0;
  long long last_sample_ns = 0;
};
HandTraceState g_hand_trace[2];
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
// One warning per outage of the camera locomotion basis: a session that loses
// its steering reference says so once instead of going quiet.
bool g_locomotion_fallback_logged = false;
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
VrPadHand g_capture_blocked[2]{};
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
  // Per-profile editor: each OpenXR profile can be switched off, and a profile
  // that is off is never suggested at all, so the runtime cannot resolve a
  // binding the operator removed. Which profiles exist is a property of the
  // runtime, not of this list -- an off profile is a smaller suggestion set,
  // never a different one.
  const Config c = DiagnosticsCurrentConfig();
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
  // WMR carries the full binding set (poses, squeeze, trigger, stick, haptics,
  // menu, stick clicks), so switching it off removes those bindings for WMR
  // devices rather than just a face button.
  const size_t profile_count = c.bind_profile_wmr ? 1u : 0u;
  for (size_t pi = 0; pi < profile_count; ++pi) {
    const char* profile = profiles[pi];
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
  if (c.bind_profile_quest3) {
    XrActionSuggestedBinding list[32]{};
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
  if (c.bind_profile_index) {
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
  if (c.bind_profile_simple) {
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

// OpenXR frame reads live in input_frame_acquisition.cpp.

bool ReleaseCapturedHand(VrPadHand& hand, VrPadHand& held) {
  if (std::fabs(held.stick_x) >= 0.2f || std::fabs(held.stick_y) >= 0.2f) {
    if (std::fabs(hand.stick_x) < 0.2f && std::fabs(hand.stick_y) < 0.2f) {
      held.stick_x = held.stick_y = 0;
    } else {
      hand.stick_x = hand.stick_y = 0;
    }
  }
  const auto release_axis = [](float& value, float& blocked) {
    if (blocked < 0.2f) return;
    if (value < 0.2f) blocked = 0;
    else value = 0;
  };
  const auto release_button = [](bool& value, bool& blocked) {
    if (!blocked) return;
    if (!value) blocked = false;
    else value = false;
  };
  // The D-pad axes are signed, so the unsigned squeeze/trigger release would
  // never mask a held down/left: -1 < 0.2 short-circuits it.
  const auto release_signed = [](float& value, float& blocked) {
    if (std::fabs(blocked) < 0.2f) return;
    if (std::fabs(value) < 0.2f) blocked = 0;
    else value = 0;
  };
  release_axis(hand.squeeze, held.squeeze);
  release_axis(hand.trigger, held.trigger);
  release_signed(hand.dpad_x, held.dpad_x);
  release_signed(hand.dpad_y, held.dpad_y);
  release_button(hand.primary, held.primary);
  release_button(hand.secondary, held.secondary);
  release_button(hand.menu, held.menu);
  release_button(hand.stick_click, held.stick_click);
  return std::fabs(held.stick_x) >= 0.2f || std::fabs(held.stick_y) >= 0.2f ||
      held.squeeze >= 0.2f || held.trigger >= 0.2f || held.primary ||
      held.secondary || held.menu || held.stick_click ||
      std::fabs(held.dpad_x) >= 0.2f || std::fabs(held.dpad_y) >= 0.2f;
}

bool SamePad(const VirtualPadState& a, const VirtualPadState& b) {
  return a.buttons == b.buttons && a.left_trigger == b.left_trigger &&
      a.right_trigger == b.right_trigger && a.thumb_lx == b.thumb_lx &&
      a.thumb_ly == b.thumb_ly && a.thumb_rx == b.thumb_rx &&
      a.thumb_ry == b.thumb_ry;
}

void RecordPadTelemetry(const VirtualPadState& pad, bool active,
                        long long predicted_ns, std::uint64_t space_epoch) {
  if (!VrTraceSessionActive()) {
    g_pad_trace.initialized = false;
    return;
  }
  const auto capture_id = VrTraceCaptureId();
  const bool changed = !g_pad_trace.initialized ||
      g_pad_trace.capture_id != capture_id || g_pad_trace.active != active ||
      !SamePad(g_pad_trace.pad, pad);
  if (!changed) return;
  g_pad_trace.pad = pad;
  g_pad_trace.active = active;
  g_pad_trace.initialized = true;
  g_pad_trace.capture_id = capture_id;
  VrTraceEvent event{};
  event.event = "virtual_pad_state";
  event.seq = ++g_pad_trace.sequence;
  event.predicted_ns = predicted_ns;
  event.epoch = space_epoch;
  event.flags = active ? 1u : 0u;
  event.values[0] = static_cast<float>(pad.buttons);
  event.values[1] = pad.left_trigger;
  event.values[2] = pad.right_trigger;
  event.values[3] = pad.thumb_lx;
  event.values[4] = pad.thumb_ly;
  event.values[5] = pad.thumb_rx;
  event.values[6] = pad.thumb_ry;
  VrTraceRecord(event);
}

void RecordHandTelemetry(const HandSample hands[2], std::uint64_t space_epoch,
                         long long predicted_ns) {
  constexpr long long kHandSampleIntervalNs = 33333334LL; // <=30 Hz
  if (!VrTraceSessionActive()) {
    for (auto& state : g_hand_trace) state.initialized = false;
    return;
  }
  const auto capture_id = VrTraceCaptureId();
  for (int side = 0; side < 2; ++side) {
    const HandSample& hand = hands[side];
    auto& state = g_hand_trace[side];
    const bool baseline = !state.initialized || state.capture_id != capture_id;
    const bool transition = baseline || state.valid != hand.valid ||
                            state.space_epoch != space_epoch;
    const bool due = hand.valid && predicted_ns > 0 &&
        (state.last_sample_ns <= 0 || predicted_ns < state.last_sample_ns ||
         predicted_ns - state.last_sample_ns >= kHandSampleIntervalNs);
    state.initialized = true;
    state.valid = hand.valid;
    state.space_epoch = space_epoch;
    state.capture_id = capture_id;
    if (!transition && !due) continue;
    VrTraceEvent event{};
    event.event = "hand_tracking";
    event.seq = ++state.sequence;
    event.predicted_ns = predicted_ns;
    event.epoch = space_epoch;
    event.flags = (hand.valid ? 1u : 0u) | (transition ? 2u : 0u) |
        ((hand.velocity_flags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) ? 4u : 0u) |
        ((hand.velocity_flags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT) ? 8u : 0u);
    event.values[0] = static_cast<float>(side); // 0=left, 1=right
    event.values[14] = static_cast<float>(
        (hand.squeeze_held ? 1u : 0u) | (hand.trigger_held ? 2u : 0u) |
        (hand.primary_held ? 4u : 0u));
    if (hand.valid) {
      for (unsigned i = 0; i < 3; ++i) event.values[1 + i] = hand.pos[i];
      for (unsigned i = 0; i < 4; ++i) event.values[4 + i] = hand.quat[i];
      for (unsigned i = 0; i < 3; ++i) event.values[8 + i] = hand.linear_velocity[i];
      for (unsigned i = 0; i < 3; ++i) event.values[11 + i] = hand.angular_velocity[i];
      state.last_sample_ns = predicted_ns;
    }
    VrTraceRecord(event);
  }
}

void PublishPad(const VirtualPadState& pad, bool active,
                long long predicted_ns, std::uint64_t space_epoch) {
  {
    std::lock_guard<std::mutex> lock(g_pad_mutex);
    g_pad_state = pad;
    g_pad_active = active;
  }
  RecordPadTelemetry(pad, active, predicted_ns, space_epoch);
  static ULONGLONG last_route_log = 0;
  const auto now = GetTickCount64();
  if (!last_route_log || now < last_route_log || now - last_route_log >= 1000) {
    last_route_log = now;
    DSVR_LOG("InputRoute: active=%d capture=%d calibration=%d rearm=%d theater=%d gameplay=%d buttons=0x%04x left=(%d,%d)",
             active, ModMenuCapturesInput(), CalibrationCapturesInput(), g_capture_rearm,
             XrMenuPresentationActive(), CameraGameplayReady(), static_cast<unsigned>(pad.buttons),
             static_cast<int>(pad.thumb_lx), static_cast<int>(pad.thumb_ly));
  }
}

void PublishArmTargets(const ArmTarget targets[2], const HandSample hands[2]) {
  std::lock_guard<std::mutex> lock(g_arm_mutex);
  g_arm_target[0] = targets[0];
  g_arm_target[1] = targets[1];
  g_hand[0] = hands[0];
  g_hand[1] = hands[1];
}

SwingAttackParams SwingParamsFromConfig(const Config& cfg) {
  SwingAttackParams p;
  p.slash_speed_min = cfg.swing_attack_speed_min;
  p.slash_heavy_min = std::max(cfg.swing_attack_heavy_min, p.slash_speed_min);
  // Every allowed light threshold must have a distinct, quieter rest band.
  p.rest_speed_max = std::min(p.rest_speed_max,
                             p.slash_speed_min * p.thrust_ratio * 0.5f);

  return p;
}

ShieldGuardParams GuardParamsFromConfig(const Config& cfg) {
  ShieldGuardParams p;
  p.guard_settle_ms = cfg.guard_settle_ms;
  p.flick_speed_min = cfg.guard_flick_speed_min;
  p.parry_press_ms = cfg.guard_parry_press_ms;
  p.parry_cooldown_ms = cfg.guard_parry_cooldown_ms;
  // The shipped defaults already separate these bands; a configured flick bar
  // must stay above the still band and below the swing band, and the parry tap
  // must fit inside its own cooldown.
  p.flick_speed_min = std::max(p.flick_speed_min, p.guard_speed_max * 1.5f);
  p.flick_speed_max = std::max(p.flick_speed_max, p.flick_speed_min * 2.0f);
  p.parry_cooldown_ms = std::max(p.parry_cooldown_ms, p.parry_press_ms * 2);
  return p;
}

// The detector's own validation refuses an impossible band and resets every
// frame, so a configured threshold that inverts a band is repaired here rather
// than silently disabling the bridge: a stride has to outpace the quiet partner
// and stay below a swing, the gait window has to outlast one fast phase, and the
// strength ramp cannot finish before the gait engages.
ArmSwingParams ArmSwingParamsFromConfig(const Config& cfg) {
  ArmSwingParams p;
  p.stride_speed_min = cfg.arm_swing_stride_min;
  p.stride_speed_max =
      std::max(cfg.arm_swing_stride_max, p.stride_speed_min * 1.5f);
  p.quiet_speed_max =
      std::min(cfg.arm_swing_quiet_max, p.stride_speed_min * 0.6f);
  p.alternations_min = cfg.arm_swing_alternations;
  p.hold_ms = cfg.arm_swing_hold_ms;
  p.stride_ms_max = std::max(p.stride_ms_max, p.stride_ms_min + 20);
  p.window_ms = std::max(p.window_ms, p.hold_ms + p.stride_ms_max);
  p.ramp_alternations = std::max(p.ramp_alternations, p.alternations_min);
  return p;
}

// Same rule for the reach envelope: the outer bound has to sit past the inner
// one and the rest band has to stay under the thrust band, or "still" and
// "thrusting" overlap and the gesture can never be told apart from a hand at
// rest.
ReachIntentParams ReachParamsFromConfig(const Config& cfg) {
  ReachIntentParams p;
  p.forward_min_m = cfg.reach_forward_min_m;
  p.forward_max_m = std::max(cfg.reach_forward_max_m, p.forward_min_m * 1.2f);
  p.speed_min = cfg.reach_speed_min;
  p.dominance_min = cfg.reach_dominance_min;
  p.quiet_max = std::min(cfg.reach_quiet_max, p.speed_min * 0.6f);
  p.settle_ms = cfg.reach_settle_ms;
  p.press_ms = cfg.reach_press_ms;
  p.cooldown_ms = std::max<long long>(cfg.reach_cooldown_ms, p.press_ms * 2);
  return p;
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
std::mutex& g_roomscale_mutex = g_locomotion_mutex;
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
    g_roomscale_motion.calibration_revision = basis.calibration_revision;
    g_roomscale_motion.world_scale = basis.world_scale;
    g_roomscale_motion.published_ms = now;
    g_roomscale_motion.expires_ms = now + 150;
    g_roomscale_motion.active = true;
    return true;
  }
  if (!g_roomscale_motion.active || now > g_roomscale_motion.expires_ms ||
      g_roomscale_motion.space_epoch != epoch) g_roomscale_motion = {};
  return false;
}


// The action set and its actions are built here rather than inline in
// InputAttachSession, so a live binding reload can rebuild the same handles on
// the input thread -- the only thread reading them once a session is live.
bool CreateActions() {
  XrActionSetCreateInfo set_info{XR_TYPE_ACTION_SET_CREATE_INFO};
  strncpy_s(set_info.actionSetName, "gameplay", _TRUNCATE);
  strncpy_s(set_info.localizedActionSetName, "DSVR gameplay", _TRUNCATE);
  set_info.priority = 0;
  if (!OkInput(xrCreateActionSet(g_instance, &set_info, &g_action_set),
               "xrCreateActionSet"))
    return false;
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
                  g_haptic))
    return false;
  SuggestAll();
  return true;
}

// Attach the set and create the pose spaces. The suggested bindings have to
// exist before a set is attached, which is exactly why a reload rebuilds a set
// instead of re-suggesting the one already in use.
bool AttachActions() {
  XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
  attach.countActionSets = 1;
  attach.actionSets = &g_action_set;
  if (!OkInput(xrAttachSessionActionSets(g_session, &attach),
               "xrAttachSessionActionSets"))
    return false;
  for (int side = 0; side < 2; ++side) {
    XrActionSpaceCreateInfo space_info{XR_TYPE_ACTION_SPACE_CREATE_INFO};
    space_info.action = g_grip_pose;
    space_info.subactionPath = g_hand_path[side];
    space_info.poseInActionSpace.orientation.w = 1.0f;
    if (!OkInput(xrCreateActionSpace(g_session, &space_info,
                                     &g_grip_space[side]),
                 "xrCreateActionSpace(grip)"))
      return false;
    space_info.action = g_aim_pose;
    if (XR_FAILED(xrCreateActionSpace(g_session, &space_info, &g_aim_space[side]))) {
      g_aim_space[side] = XR_NULL_HANDLE;
      DSVR_WARN("Input: aim space unavailable; controller navigation remains available");
    }
  }
  return true;
}

}  // namespace

// Report the pending binding edit without mutating immutable attached actions.
void InputRequestBindingReload() {
  g_binding_reload.store(true, std::memory_order_release);
}

// The profile set the live action set was built with.
InputBindingSignature InputLiveBindingSignature() {
  return {g_binding_attached.quest3, g_binding_attached.index,
          g_binding_attached.wmr, g_binding_attached.simple};
}

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

  if (!CreateActions() || !AttachActions()) {
    g_failed = true;
    return;
  }
  SyncParams(cfg);
  InputInvalidateHeadDirectedLocomotion();
  g_binding_seen = {cfg.bind_profile_quest3, cfg.bind_profile_index,
                    cfg.bind_profile_wmr, cfg.bind_profile_simple};
  g_binding_seen_valid = true;
  g_binding_attached = g_binding_seen;
  g_binding_attached_valid = true;
  g_attached = true;
  DSVR_LOG("Input: OpenXR actions attached (Touch/Index/WMR/simple "
           "bindings suggested). Continuous hands for IK arms; no swing "
           "triggers, no game writes.");
}

bool InputAvailable() { return g_attached && !g_failed; }

static void InvalidateInputFrame(std::uint64_t space_epoch, long long predicted_ns) {
  g_comfort_motion = {};
  const ArmTarget targets[2]{};
  const HandSample hands[2]{};
  RecordHandTelemetry(hands, space_epoch, predicted_ns);
  PublishArmTargets(targets, hands);
  PublishPad({}, false, predicted_ns, space_epoch);
  InputInvalidateHeadDirectedLocomotion();
  g_pending_turn_yaw.store(0.0f, std::memory_order_relaxed);
  g_combos.Reset();
  g_sanctuary_chord.Reset();
  g_turn.Reset();
  g_stick_dpad.Reset();
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
    g_swing_attack[0].Reset();
    g_swing_attack[1].Reset();
  g_shield_guard[0].Reset();
  g_shield_guard[1].Reset();
  g_arm_swing.Reset();
  g_reach[0].Reset();
  g_reach[1].Reset();
  g_reach_hold_until_ms[0] = g_reach_hold_until_ms[1] = 0;
  std::lock_guard<std::mutex> lock(g_selection_mutex);
  g_selection_store.Invalidate();
}

void InputInvalidateFrame() {
  InvalidateInputFrame(0, 0);
}

void InputPublishInteractionSelection(const InteractionSelection& selection,
                                      long long now_ms) {
  std::lock_guard<std::mutex> lock(g_selection_mutex);
  g_selection_store.Publish(selection, now_ms);
}

void InputInvalidateInteractionSelection() {
  std::lock_guard<std::mutex> lock(g_selection_mutex);
  g_selection_store.Invalidate();
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

void InputPublishCameraMotionBasis(const RoomscaleBasis& roomscale,
                                   Vec3 head_forward,
                                   Vec3 movement_basis_forward,
                                   std::uint64_t space_epoch) {
  HeadDirectedLocomotion locomotion{};
  VrHeading heading;
  if (space_epoch && heading.SeedOnce(head_forward) &&
      heading.CandidateLocomotion(0.0f, 0.0f, movement_basis_forward).valid) {
    locomotion.head_forward = head_forward;
    locomotion.movement_basis = movement_basis_forward;
    locomotion.space_epoch = space_epoch;
    locomotion.published_ms = GetTickCount64();
    locomotion.valid = true;
  }
  const bool valid_roomscale = roomscale.space_epoch == space_epoch && space_epoch &&
      FiniteVec(roomscale.tracking_right) &&
      std::isfinite(roomscale.world_scale) && roomscale.world_scale > 0.f &&
      FiniteVec(roomscale.tracking_up) && FiniteVec(roomscale.tracking_forward) &&
      FiniteVec(roomscale.movement_basis) && VLen(roomscale.tracking_right) >= 0.9f &&
      VLen(roomscale.tracking_up) >= 0.9f && VLen(roomscale.tracking_forward) >= 0.9f &&
      VLen(roomscale.movement_basis) >= 0.5f;
  std::lock_guard<std::mutex> lock(g_locomotion_mutex);
  g_head_directed_locomotion = locomotion;
  if (!valid_roomscale) {
    g_roomscale_basis = {};
    ResetRoomscaleLocked();
    return;
  }
  g_roomscale_basis = roomscale;
  g_roomscale_basis.published_ms = GetTickCount64();
  g_roomscale_basis.valid = true;
  if (g_roomscale_sampler.epoch != roomscale.space_epoch ||
      g_roomscale_sampler.calibration_revision != roomscale.calibration_revision ||
      g_roomscale_sampler.world_scale != roomscale.world_scale) ResetRoomscaleLocked();
}

bool InputLatestRoomscaleMotion(RoomscaleMotion& out) {
  out = {};
  std::lock_guard<std::mutex> lock(g_roomscale_mutex);
  const auto now = GetTickCount64();
  if (!g_roomscale_basis.valid || !g_roomscale_motion.active ||
      g_roomscale_motion.space_epoch != g_roomscale_basis.space_epoch ||
      g_roomscale_motion.calibration_revision != g_roomscale_basis.calibration_revision ||
      g_roomscale_motion.world_scale != g_roomscale_basis.world_scale ||
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
  g_roomscale_basis = {};
  ResetRoomscaleLocked();
}

void InputCopyTrackingSamples(HandSample hands[2], ArmTarget arms[2]) {
  std::lock_guard<std::mutex> lock(g_arm_mutex);
  for (int side = 0; side < 2; ++side) {
    hands[side] = g_hand[side];
    arms[side] = g_arm_target[side];
  }
}

void LogBridgeReadiness(const Config& cfg, bool motion_bridge_armed,
                          const char* attack_gate, const char* defence_gate,
                          const char* suppress_reason,
                          const SwingAttackOutput& swing_attack,
                          const ShieldGuardOutput& shield_guard,
                          const ArmIkHands& committed_hands,
                          std::uint64_t space_epoch) {
  // Log-only diagnostic: one line per state change, never per frame. Names the
  // exact gate that is closed so a single live session identifies whether the
  // bridges are config-disabled, waiting on IK commits, or actively firing.
  static std::string last_line;
  char line[512]{};
  std::snprintf(line, sizeof(line),
                "motion=%d attack=%s defence=%s suppress=%s swing=%d/%d guard=%d/%d "
                "hands=%llu hands_epoch=%llu space_epoch=%llu motion_attacks=%d "
                "physical_attacks=%d physical_defence=%d",
                motion_bridge_armed ? 1 : 0, attack_gate, defence_gate,
                suppress_reason, swing_attack.light ? 1 : 0,
                swing_attack.heavy ? 1 : 0, shield_guard.block ? 1 : 0,
                shield_guard.parry ? 1 : 0,
                static_cast<unsigned long long>(committed_hands.sequence),
                static_cast<unsigned long long>(committed_hands.epoch),
                static_cast<unsigned long long>(space_epoch),
                cfg.motion_attacks ? 1 : 0, cfg.physical_attacks ? 1 : 0,
                cfg.physical_defence ? 1 : 0);
  if (line != last_line) {
    last_line = line;
    DSVR_LOG("InputBridges: %s", line);
  }
}

static void ApplyInputFramePolicy(const Config& cfg, Vec3 head,
    std::uint64_t space_epoch, long long predicted_display_time, bool sanctuary,
    const OpenXrInputFrame& acquired) {
  float dt = 0.0f;
  if (g_last_time_ns > 0)
    dt = (float)(predicted_display_time - g_last_time_ns) * 1e-9f;
  g_last_time_ns = predicted_display_time;


  if (CombatIdentityChanged()) ResetPhysicalBridges();
  const HandParams hand_params;
  // Head facing in the tracking frame. The reach envelope is measured along the
  // direction the player is actually looking, in the same space and at the same
  // predicted time as the hand poses, so a reach means reaching where the head
  // points rather than wherever the hand happens to swing.
  const bool head_facing = acquired.view_orientation_valid;
  const Vec3 head_forward = head_facing
      ? VNorm(QuatRotate(Vec3{0.f, 0.f, -1.f}, acquired.view_orientation[0],
                         acquired.view_orientation[1], acquired.view_orientation[2],
                         acquired.view_orientation[3]))
      : Vec3{};
  VrPadInput pad_input = acquired.actions;
  bool pad_active = acquired.pad_active;
  bool right_stick_active = acquired.right_stick_active;
  bool right_stick_failed = acquired.right_stick_failed;
  ArmTarget arm_targets[2]{};
  HandSample hand_samples[2]{};
  SwingAttackOutput swing_attack{};
    ShieldGuardOutput shield_guard{};
    // Physical-bridge readiness, logged once per change by LogBridgeReadiness
    // below. These are the gates that decide whether swings/guards request
    // native input; logging them here turns a silent gate into a checkable
    // fact. No behavior change.
    bool motion_bridge_armed = false;
    const char* attack_gate = "uncomputed";
    const char* defence_gate = "uncomputed";
    const char* suppress_reason = "uncomputed";
    // Reach geometry per hand, consumed after the loop once the opposite hand's
    // speed for this same frame is known.
    float reach_offset[2]{};
    float reach_forward_speed[2]{};
    float reach_speed[2]{};
    bool reach_ready[2] = {false, false};
    const int weapon_side = cfg.dominant_hand_left ? 0 : 1;
    const int shield_side = weapon_side == 0 ? 1 : 0;
    ArmIkHands committed_hands{};
    const bool ik_hands_available = ArmIkLatestHands(committed_hands);
    motion_bridge_armed = !sanctuary && cfg.motion_attacks &&
        !ModMenuCapturesInput() && !CalibrationCapturesInput() && !g_capture_rearm;
    const char* bridge_gate = nullptr;
    if (sanctuary) bridge_gate = "theater-menu-active";
    else if (!CameraGameplayReady()) bridge_gate = "camera-not-gameplay";
    else if (cfg.observer_only) bridge_gate = "observer-only";
    else if (!cfg.arm_ik_enabled) bridge_gate = "arm_ik-off";
    else if (cfg.arm_ik_log_only) bridge_gate = "arm_ik-log-only";
    else if (!cfg.arm_ik_block_animation) bridge_gate = "block_animation-off";
    else if (!ik_hands_available) bridge_gate = "no-committed-hands";
    else if (committed_hands.epoch != space_epoch) bridge_gate = "hand-epoch-stale";
    if (bridge_gate) attack_gate = bridge_gate;
    else if (!cfg.motion_attacks) attack_gate = "motion_attacks-off";
    else if (!cfg.physical_attacks) attack_gate = "physical_attacks-off";
    else if (!committed_hands.side[weapon_side].valid) attack_gate = "weapon-hand-invalid";
    else attack_gate = "ready";
    const bool physical_attack_ready = std::strcmp(attack_gate, "ready") == 0;
    if (bridge_gate) defence_gate = bridge_gate;
    else if (!cfg.physical_defence) defence_gate = "physical_defence-off";
    else if (!committed_hands.side[shield_side].valid) defence_gate = "shield-hand-invalid";
    else defence_gate = "ready";
      const bool physical_defence_ready = std::strcmp(defence_gate, "ready") == 0;

  for (int side = 0; side < 2; ++side) {
    VrPadHand& pad_hand = side == 0 ? pad_input.left : pad_input.right;
    const AcquiredHand& raw_hand = acquired.hands[side];
    HandSample& sample = hand_samples[side];
    sample.valid = raw_hand.grip_valid;
    for (int axis = 0; axis < 3; ++axis) {
      sample.pos[axis] = raw_hand.grip_position[axis];
      sample.aim_pos[axis] = raw_hand.aim_position[axis];
      sample.linear_velocity[axis] = raw_hand.linear_velocity[axis];
      sample.angular_velocity[axis] = raw_hand.angular_velocity[axis];
    }
    for (int axis = 0; axis < 4; ++axis) {
      sample.quat[axis] = raw_hand.grip_orientation[axis];
      sample.aim_quat[axis] = raw_hand.aim_orientation[axis];
    }
    sample.aim_valid = raw_hand.aim_valid;
    sample.velocity_flags = raw_hand.velocity_flags;
    sample.space_epoch = space_epoch;
    sample.predicted_ns = predicted_display_time;
    const float squeeze_raw = pad_hand.squeeze;
    const float trigger_raw = pad_hand.trigger;
    sample.squeeze_held = squeeze_raw > 0.7f;
    sample.trigger_held = trigger_raw > 0.7f;
    sample.primary_held = pad_hand.primary;
    if (!sample.valid) {
      g_blade[side].Reset();
      g_swing_attack[side].Reset();
      g_shield_guard[side].Reset();
      continue;
    }
    const Vec3 blade = BladeDirection(sample.quat[0], sample.quat[1],
                                      sample.quat[2], sample.quat[3]);
    sample.blade_dir[0] = blade.x;
    sample.blade_dir[1] = blade.y;
    sample.blade_dir[2] = blade.z;
    const Vec3 grip{sample.pos[0], sample.pos[1], sample.pos[2]};
    const Vec3 tip = BladeTip(grip, sample.quat[0], sample.quat[1],
        sample.quat[2], sample.quat[3], hand_params.blade_length_m);
    sample.tip[0] = tip.x;
    sample.tip[1] = tip.y;
    sample.tip[2] = tip.z;
    sample.xr_speed = -1.0f;
    if (raw_hand.linear_velocity_valid)
      sample.xr_speed = VLen({sample.linear_velocity[0], sample.linear_velocity[1],
                              sample.linear_velocity[2]});
      // Head-relative kinematics feed the contact bench and optional native-input
    // preview. Neither submits physical contact damage. Menus discard both
    // histories; disable the bridges only when all motion paths are off.
    // Velocities are head-relative: roomscale walking moves head and
    // hands together and must not read as a swing.
    if (sanctuary || ModMenuCapturesInput() || CalibrationCapturesInput() ||
        g_capture_rearm ||
        (!cfg.motion_attacks && !cfg.arm_swing_sprint &&
           !cfg.physical_defence)) {
      g_blade[side].Reset();
      g_swing_attack[side].Reset();
      g_shield_guard[side].Reset();
      g_reach[side].Reset();
      g_reach_hold_until_ms[side] = 0;
      sample.speed = 0.0f;
    } else {
      const auto& previous = g_blade[side];
      const auto same_position = [](Vec3 a, Vec3 b) {
        return a.x == b.x && a.y == b.y && a.z == b.z;
      };
      // A repeated callback for the same pose/time is not tracking loss.
      // Preserve the velocity and bounded button holds, without accumulating
      // motion twice. A changed pose at the same time still fails closed.
      const bool duplicate_sample = dt == 0.0f && previous.valid &&
          previous.space_epoch_ == space_epoch &&
          same_position(previous.grip, grip) && same_position(previous.tip, tip) &&
          same_position(previous.head, head);
      if (!duplicate_sample &&
          (!previous.valid || previous.space_epoch_ != space_epoch ||
           !(dt > 0.0f) || dt > 0.25f)) {
        g_swing_attack[side].Reset();
        g_shield_guard[side].Reset();
        g_reach[side].Reset();
        g_reach_hold_until_ms[side] = 0;
      }
      if (!duplicate_sample)
        g_blade[side].FeedRelative(grip, tip, head, dt, space_epoch);
      sample.speed = g_blade[side].grip_speed;
      // Input preview only: request a native attack from a committed IK hand.
      // Native hit windows and weapon/hurt geometry still need live proof.
      const bool weapon_hand = side == weapon_side;
      if (!physical_attack_ready || !weapon_hand || !g_blade[side].valid ||
                !g_blade[side].HasVelocitySample()) {
        g_swing_attack[side].Reset();
      } else {
        const bool thrust =
            cfg.thrust_enabled && g_blade[side].FrameKind(blade) == GestureKind::Thrust;
        swing_attack = g_swing_attack[side].Feed(
            g_blade[side].tip_speed, thrust, predicted_display_time / 1000000LL,
            SwingParamsFromConfig(cfg));
        if (swing_attack.fired)
          DSVR_LOG("Combat: physical %s at %.2f m/s -> %s attack",
                   thrust ? "thrust" : "slash", swing_attack.peak_speed,
                   swing_attack.heavy ? "strong" : "light");
      }
        // Defence preview: the shield hand's own tracked pose drives the native
      // shield button -- held for a guard wherever it is held, tapped for a
      // parry on a flick. Like the attack bridge this requests ordinary
      // native input only: guard windows, chip damage and parry judgement
      // stay the game's, and no contact geometry or damage is submitted.
      if (!physical_defence_ready || side != shield_side || !g_blade[side].valid) {
        g_shield_guard[side].Reset();
      } else {
        const float below_head = head.y - grip.y;
        const bool raised = std::isfinite(below_head) &&
            below_head <= kGuardHandBelowHeadM &&
            below_head >= -kGuardHandAboveHeadM;
        shield_guard = g_shield_guard[side].Feed(
            g_blade[side].grip_speed, raised,
            predicted_display_time / 1000000LL, GuardParamsFromConfig(cfg));
        if (shield_guard.flicked)
          DSVR_LOG("Combat: shield flick at %.2f m/s -> parry tap",
                   shield_guard.peak_speed);
      }
      // Reach geometry for this hand. Speeds are already head-relative, so
      // walking the room or riding a lift moves head and hand together and
      // cannot push a hand into the envelope.
      if (head_facing && g_blade[side].valid) {
        const Vec3 ahead = VAdd(grip, VScale(head, -1.f));
        reach_offset[side] = VDot(ahead, head_forward);
        reach_forward_speed[side] = VDot(g_blade[side].grip_velocity, head_forward);
        reach_speed[side] = g_blade[side].grip_speed;
        reach_ready[side] = true;
      }
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

  RecordHandTelemetry(hand_samples, space_epoch, predicted_display_time);
  PublishArmTargets(arm_targets, hand_samples);
  // Iteration 2: drain bench haptic pulses on the render-thread poll frame
  // every frame (the Combat panel only polls when open). No-op unless the
  // bench queued a pulse from classified geometry.
  ArmIkDrainBenchHaptics();

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
  if (capture && !stick_menu_consumed && !recalibration_consumed) {
    // Stick navigation stays raw in spatial UI; X/Y step its panel list.
    ModMenuControllerInput(pad_input.left.stick_x, pad_input.left.stick_y,
                           pad_input.right.primary, pad_input.right.secondary,
                           pad_input.left.primary, pad_input.left.secondary);
  }
  const bool consuming_controls = capture || menu_chord || stick_menu_consumed ||
      recalibration_consumed;
  if (consuming_controls) {
    g_capture_blocked[0] = pad_input.left;
    g_capture_blocked[1] = pad_input.right;
    g_capture_rearm = true;
  } else {
    if (!pad_input.left.stick_click && !pad_input.right.stick_click)
      g_stick_menu_chord_fired = false;
    if (!pad_input.left.stick_click && !pad_input.right.primary)
      g_recalibration_fired = false;
    // Each captured control needs its own release. A held navigation stick or
    // grip must not silence an unrelated, newly pressed A/B or the other stick.
    const bool left_pending = ReleaseCapturedHand(pad_input.left, g_capture_blocked[0]);
    const bool right_pending = ReleaseCapturedHand(pad_input.right, g_capture_blocked[1]);
    g_capture_rearm = left_pending || right_pending;
  }
  if (consuming_controls) {
    PublishPad({}, pad_active, predicted_display_time, space_epoch);
    g_stick_dpad.Reset();
    g_turn.Reset();
    g_combos.Reset();
    g_sanctuary_chord.Reset();
    g_pending_turn_yaw.store(0.0f, std::memory_order_relaxed);
    g_blade[0].Reset();
    g_blade[1].Reset();
    g_swing_attack[0].Reset();
    g_swing_attack[1].Reset();
    g_shield_guard[0].Reset();
    g_shield_guard[1].Reset();
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
  const bool suppress_on = cfg.arm_ik_enabled && !cfg.arm_ik_log_only &&
      combat.physical_damage_bound && combat.shield_contact_bound;
  pad_input.suppress_weapon_attacks = suppress_on;
  if (!cfg.arm_ik_enabled) suppress_reason = "arm_ik-off";
  else if (cfg.arm_ik_log_only) suppress_reason = "arm_ik-log-only";
  else if (!combat.physical_damage_bound) suppress_reason = "damage-unbound";
  else if (!combat.shield_contact_bound) suppress_reason = "shield-unbound";
  else suppress_reason = "suppressed";
  // Menu navigation is a d-pad mapping and needs no orientation reference at
    // all, so it must never be fed a stick the locomotion gate already cleared.
    // Sanctuary and every non-gameplay state are menu navigation.
    const bool menu_navigation = sanctuary || !CameraGameplayReady();
    bool stick_dpad_consumed = false;
    if (menu_navigation) g_stick_dpad.Reset();
    else stick_dpad_consumed = g_stick_dpad.Apply(pad_input.left, predicted_display_time / 1000000LL);
    if (!menu_navigation) {
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
      // Losing the camera basis removes head-directed steering, not the
        // player's controls. Zeroing the stick here is what left a live session
      // unable to move or select menu options while look, arms and the camera
      // all kept working: the raw stick is exactly what native interprets
      // against its own camera, so it is the safe and honest fallback.
      if (!g_locomotion_fallback_logged) {
        g_locomotion_fallback_logged = true;
        DSVR_WARN("Input: no fresh camera locomotion basis (%s); the left stick "
                  "stays raw until the follow-cam publishes one",
                  !locomotion.valid
                      ? "never published"
                      : locomotion.space_epoch != space_epoch ? "epoch mismatch"
                                                             : "stale");
      }
    } else {
      g_locomotion_fallback_logged = false;
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
      }
        // An unsolvable candidate (degenerate basis, non-finite stick) leaves
      // the raw stick in place. Deleting locomotion is never the answer to a
      // missing reference, and PadAxis already maps a non-finite stick to
      // centre.
      // Arm-swing gait: one more forward amount in the same movement basis as
      // the stick, so it composes rather than replaces. A player who also
      // holds the stick forward still gets the full native dash, and
      // acceleration, stamina, turning and the dash stay the game's.
      if (cfg.arm_swing_sprint && !stick_dpad_consumed && CameraGameplayReady() && !cfg.observer_only) {
        const auto gait = g_arm_swing.Feed(
            hand_samples[0].speed, hand_samples[1].speed,
            predicted_display_time / 1000000LL, ArmSwingParamsFromConfig(cfg));
        if (gait.strength > 0.0f) {
          pad_input.left.stick_y = std::clamp(pad_input.left.stick_y + gait.strength,
                                              -1.0f, 1.0f);
          pad_active = true;
        }
      } else {
        g_arm_swing.Reset();
      }
    }
  }
  // Reach-to-interact: a forward thrust of one hand taps the native interact
    // button for reach_press_ms. Both hands' speeds for this frame are known
    // here, so the "other hand stayed still" test is same-frame rather than
    // last-frame.
    {
    const long long reach_now_ms = predicted_display_time / 1000000LL;
    if (cfg.reach_interact && head_facing) {
      const ReachIntentParams reach_params = ReachParamsFromConfig(cfg);
      for (int side = 0; side < 2; ++side) {
        if (!reach_ready[side]) {
          g_reach[side].Reset();
          continue;
        }
        const auto intent = g_reach[side].Feed(
            reach_offset[side], reach_forward_speed[side], reach_speed[side],
            reach_ready[1 - side] ? reach_speed[1 - side] : 0.0f, reach_now_ms,
            reach_params);
        // Eligibility gate (H2): the gesture alone must not decide once a
        // native prompt/LOS owner exists. The selection store has no
        // producer yet, so it stays empty and the gate keeps refusing;
        // the reach path logs why instead of silently tapping A at a
        // wall or a face.
          InteractionSelectionStore selection_copy;
          {
            std::lock_guard<std::mutex> lock(g_selection_mutex);
            selection_copy = g_selection_store;
          }
          if (intent.pressed &&
              !SelectTargetEligibleWithStoredSelection(
                  InteractionTargetEligibility{}, selection_copy, reach_now_ms)) {
            g_reach_hold_until_ms[side] = 0;
          DSVR_LOG("Interaction: reach %.2f m/s at %.2f m ahead refused "
                   "(no eligible target: prompt/LOS/lifecycle/selection unbound)",
                   reach_forward_speed[side], reach_offset[side]);
          continue;
        }
        if (intent.pressed) {
          g_reach_hold_until_ms[side] = reach_now_ms + intent.press_ms;
          DSVR_LOG("Interaction: reach %.2f m/s at %.2f m ahead -> interact tap",
                   reach_forward_speed[side], reach_offset[side]);
        }
      }
    } else {
      g_reach[0].Reset();
      g_reach[1].Reset();
      g_reach_hold_until_ms[0] = g_reach_hold_until_ms[1] = 0;
    }
    for (int side = 0; side < 2; ++side)
      if (g_reach_hold_until_ms[side] > reach_now_ms) pad_input.reach_interact = true;
    }
    if (!menu_navigation)
    g_comfort_motion.move = std::hypot(pad_input.left.stick_x, pad_input.left.stick_y);
  // The opt-in swing preview adds native input without disabling manual combat.
  if (swing_attack.light) pad_input.swing_buttons |= kPadRightShoulder;
  if (swing_attack.heavy) pad_input.swing_heavy = true;
  // The physical guard is a line the caller holds down, not a pulse: it stays
  // set for every frame of the guard and clears the frame the hand drops.
  pad_input.guard_block = shield_guard.block;
  pad_input.guard_parry = shield_guard.parry;
  {
    const bool roomscale_active = UpdateRoomscaleInput(
        pad_input, head, space_epoch, predicted_display_time, cfg, sanctuary);
    PublishPad(MapVrPad(pad_input, menu_navigation),
        pad_active || roomscale_active || swing_attack.light ||
            swing_attack.heavy || shield_guard.block || shield_guard.parry ||
            pad_input.reach_interact,
        predicted_display_time, space_epoch);
  }
  LogBridgeReadiness(cfg, motion_bridge_armed, attack_gate, defence_gate,
                     suppress_reason, swing_attack, shield_guard,
                     committed_hands, space_epoch);

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
    g_comfort_motion.turn_radians = yaw;
    if (yaw != 0.0f) {
      float old = g_pending_turn_yaw.load(std::memory_order_relaxed);
      while (!g_pending_turn_yaw.compare_exchange_weak(
          old, old + yaw, std::memory_order_relaxed)) {
      }
    }
  }
}

void InputPollFrame(void* base_space, const float head_pos[3],
                    std::uint64_t space_epoch,
                    long long predicted_display_time) {
  g_comfort_motion = {};
  if (!InputAvailable() || !base_space || !head_pos ||
      predicted_display_time <= 0) {
    InvalidateInputFrame(space_epoch, predicted_display_time > 0 ? predicted_display_time : 0);
    return;
  }
  const Vec3 head{head_pos[0], head_pos[1], head_pos[2]};
  if (!FiniteVec(head)) {
    InvalidateInputFrame(space_epoch, predicted_display_time > 0 ? predicted_display_time : 0);
    return;
  }
  Config cfg = DiagnosticsCurrentConfig();
  SyncParams(cfg);

  // OpenXR attaches action sets once per session. Never destroy a working set
  // to apply an edit that the runtime must refuse on this same session.
  const bool profile_set_changed =
      cfg.bind_profile_quest3 != g_binding_seen.quest3 ||
      cfg.bind_profile_index != g_binding_seen.index ||
      cfg.bind_profile_wmr != g_binding_seen.wmr ||
      cfg.bind_profile_simple != g_binding_seen.simple;
  const bool reload_requested =
      g_binding_reload.exchange(false, std::memory_order_acq_rel);
  if (g_binding_seen_valid && (profile_set_changed || reload_requested))
    DSVR_WARN("Input: binding changes require a game restart; the attached "
              "controller actions remain active. OpenXR permits only one "
              "action-set attachment per session.");
  g_binding_seen = {cfg.bind_profile_quest3, cfg.bind_profile_index,
                   cfg.bind_profile_wmr, cfg.bind_profile_simple};
  g_binding_seen_valid = true;

  const bool sanctuary = XrMenuPresentationActive();
  OpenXrInputBindings bindings{};
  bindings.session = g_session;
  bindings.action_set = g_action_set;
  bindings.squeeze = g_squeeze;
  bindings.trigger = g_trigger;
  bindings.stick = g_stick;
  bindings.primary = g_primary;
  bindings.secondary = g_secondary;
  bindings.menu = g_menu;
  bindings.stick_click = g_stick_click;
  bindings.aim_pose = g_aim_pose;
  bindings.view_space = g_view_space;
  for (int side = 0; side < 2; ++side) {
    bindings.hand_paths[side] = g_hand_path[side];
    bindings.grip_spaces[side] = g_grip_space[side];
    bindings.aim_spaces[side] = g_aim_space[side];
  }
  OpenXrInputFrame acquired{};
  const auto acquisition = AcquireOpenXrInputFrame(bindings,
      (XrSpace)base_space, space_epoch, (XrTime)predicted_display_time,
      !sanctuary && cfg.reach_interact, acquired);
  if (acquisition.status == OpenXrInputFrameStatus::NotFocused) {
    InvalidateInputFrame(space_epoch, predicted_display_time);
    return;
  }
  if (acquisition.status == OpenXrInputFrameStatus::SyncFailed) {
    OkInput(acquisition.sync_result, "xrSyncActions");
    InvalidateInputFrame(space_epoch, predicted_display_time);
    g_failed = true;
    return;
  }

  ApplyInputFramePolicy(cfg, head, space_epoch, predicted_display_time,
                         sanctuary, acquired);
}

ComfortMotion InputLatestComfortMotion() { return g_comfort_motion; }

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
