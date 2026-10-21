#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace dsvr {

// XInput-compatible button values kept local so this pure mapping stays
// platform-header-free and can be exercised without the game or OpenXR.
constexpr std::uint16_t kPadDpadUp = 0x0001;
constexpr std::uint16_t kPadDpadDown = 0x0002;
constexpr std::uint16_t kPadDpadLeft = 0x0004;
constexpr std::uint16_t kPadDpadRight = 0x0008;
constexpr std::uint16_t kPadStart = 0x0010;
constexpr std::uint16_t kPadBack = 0x0020;
constexpr std::uint16_t kPadLeftThumb = 0x0040;
constexpr std::uint16_t kPadRightThumb = 0x0080;
constexpr std::uint16_t kPadLeftShoulder = 0x0100;
constexpr std::uint16_t kPadRightShoulder = 0x0200;
constexpr std::uint16_t kPadA = 0x1000;
constexpr std::uint16_t kPadB = 0x2000;
constexpr std::uint16_t kPadX = 0x4000;
constexpr std::uint16_t kPadY = 0x8000;

struct VrPadHand {
  float stick_x = 0.0f;
  float stick_y = 0.0f;
  float squeeze = 0.0f;
  float trigger = 0.0f;
  bool primary = false;
  bool secondary = false;
  bool menu = false;
  bool stick_click = false;
};

struct VrPadInput {
  VrPadHand left;
  VrPadHand right;
  bool dominant_left = false;  // weapon hand selection (config)
  // DEPRECATED (pre-IK interim): classified-swing attack pulse. Retired
  // by the IK design -- input.cpp no longer sets these. Kept so old
  // tests/callers still compile; MapVrPad ignores them when
  // suppress_weapon_attacks is set.
  std::uint16_t pulse_buttons = 0;
  bool pulse_heavy = false;
  // IK arms on: weapon-hand squeeze/trigger must NOT fire RB/RT canned
  // attacks (the arm itself is the attack). Off-hand, buttons, sticks
  // and menus are unaffected.
  bool suppress_weapon_attacks = false;

  float roomscale_x = 0.0f;  // already in native movement axes
  float roomscale_y = 0.0f;
};


struct RoomscaleAxes {
  float x = 0.0f;
  float y = 0.0f;
  bool active = false;
};

// Convert native-frame physical walking speed to an analog movement intent.
// Below the deadzone this is exactly neutral; faster steps saturate without
// changing direction, and invalid tracking/configuration fails closed.
inline RoomscaleAxes RoomscaleAxesFromVelocity(float right_mps,
                                                float forward_mps,
                                                float world_scale = 1.0f,
                                                float deadzone_mps = 0.12f,
                                                float full_speed_mps = 1.2f) {
  if (!std::isfinite(right_mps) || !std::isfinite(forward_mps) ||
      !std::isfinite(world_scale) || !(world_scale > 0.0f) ||
      !std::isfinite(deadzone_mps) || deadzone_mps < 0.0f ||
      !std::isfinite(full_speed_mps) || full_speed_mps <= deadzone_mps)
    return {};
  const float x = right_mps * world_scale;
  const float y = forward_mps * world_scale;
  const float speed = std::hypot(x, y);
  if (!std::isfinite(speed) || speed <= deadzone_mps) return {};
  const float magnitude = std::clamp((speed - deadzone_mps) /
      (full_speed_mps - deadzone_mps), 0.0f, 1.0f);
  if (!(magnitude > 0.0f)) return {};
  return {x / speed * magnitude, y / speed * magnitude, true};
}

struct VirtualPadState {
  std::uint16_t buttons = 0;
  std::uint8_t left_trigger = 0;
  std::uint8_t right_trigger = 0;
  std::int16_t thumb_lx = 0;
  std::int16_t thumb_ly = 0;
  std::int16_t thumb_rx = 0;
  std::int16_t thumb_ry = 0;
};

inline std::int16_t PadAxis(float value) {
  if (!std::isfinite(value)) return 0;
  if (value <= -1.0f) return -32768;
  if (value >= 1.0f) return 32767;
  const float scaled = value * 32767.0f;
  return static_cast<std::int16_t>(scaled >= 0.0f ? scaled + 0.5f
                                                  : scaled - 0.5f);
}

inline std::uint8_t PadTrigger(float value) {
  if (!std::isfinite(value) || value <= 0.0f) return 0;
  if (value >= 1.0f) return 255;
  return static_cast<std::uint8_t>(value * 255.0f + 0.5f);
}

// Motion-controller policy:
// - left stick feeds native locomotion;
// - right stick stays out of XInput because turn_math owns VR turning;
// - face/menu/stick-click buttons preserve navigation, dodge, items,
//   interaction, lock-on and the game menu;
// - IK arms on: ALL canned combat outputs are nullified. Weapon-hand
//   squeeze/trigger (RB/RT attacks), off-hand squeeze (LB block) and
//   off-hand trigger (LT parry/art) map to nothing: attacking,
//   blocking and parrying are the physical arms, solved by IK and
//   judged by tracked contact geometry. Firing a canned animation
//   while the IK body holds a physical guard would double-drive the
//   arms and detach the head from the body.
// - IK arms off (legacy bridge only): weapon hand squeeze/trigger =
//   RB/RT, off-hand squeeze = LB, off-hand trigger = LT.
inline VirtualPadState MapVrPad(const VrPadInput& input,bool menu_navigation=false) {
  VirtualPadState out;
  if(menu_navigation) {
    // Native DSR menu lists consume D-pad, rather than locomotion axes.
    const float x=input.left.stick_x,y=input.left.stick_y;
    if(std::isfinite(x) && std::isfinite(y)) {
      if(std::fabs(y)>=std::fabs(x) && std::fabs(y)>.55f)
        out.buttons|=y>0?kPadDpadUp:kPadDpadDown;
      else if(std::fabs(x)>.55f)out.buttons|=x>0?kPadDpadRight:kPadDpadLeft;
    }
  } else {
    out.thumb_lx = PadAxis(input.left.stick_x + input.roomscale_x);
    out.thumb_ly = PadAxis(input.left.stick_y + input.roomscale_y);
  }

  if (input.left.primary) out.buttons |= kPadX;
  if (input.left.secondary) out.buttons |= kPadY;
  if (input.right.primary) out.buttons |= kPadA;
  if (input.right.secondary) out.buttons |= kPadB;
  if (input.left.menu || input.right.menu) out.buttons |= kPadStart;
  if (input.left.stick_click) out.buttons |= kPadLeftThumb;
  if (input.right.stick_click) out.buttons |= kPadRightThumb;

  const VrPadHand& weapon = input.dominant_left ? input.left : input.right;
  const VrPadHand& off = input.dominant_left ? input.right : input.left;
  if (!input.suppress_weapon_attacks) {
    if (weapon.squeeze > 0.5f) out.buttons |= kPadRightShoulder;
    out.right_trigger = PadTrigger(weapon.trigger);
    out.buttons |= input.pulse_buttons;
    if (input.pulse_heavy) out.right_trigger = 255;
      if (off.squeeze > 0.5f) out.buttons |= kPadLeftShoulder;
      out.left_trigger = PadTrigger(off.trigger);
  }
  // suppress_weapon_attacks (IK arms on): RB/RT/LB/LT all stay zero.
  // The physical arms ARE the attack, block and parry; any canned
  // native animation would fight the IK solve for the same bones.
  return out;
}

// Merge at the XInput seam. Motion buttons augment a real controller, while
// the tracked left stick owns locomotion even at centre (otherwise a resting
// physical stick could leak drift into VR). Shoulders merge as buttons;
// a motion trigger wins only while actually pulled, so a resting Quest
// trigger never zeroes a physical controller's analogue input.
inline VirtualPadState OverlayMotionPad(VirtualPadState physical,
                                        const VirtualPadState& motion) {
  physical.buttons |= motion.buttons;
  physical.thumb_lx = motion.thumb_lx;
  physical.thumb_ly = motion.thumb_ly;
  if (motion.left_trigger > physical.left_trigger)
    physical.left_trigger = motion.left_trigger;
  if (motion.right_trigger > physical.right_trigger)
    physical.right_trigger = motion.right_trigger;
  return physical;
}

}  // namespace dsvr
