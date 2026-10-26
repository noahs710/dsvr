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
  // Logical D-pad axes, supplied by StickDpad for motion controllers.
  // Native menu navigation also accepts the unmodified left stick.
  float dpad_x = 0.0f;
  float dpad_y = 0.0f;
};
// Hold left stick-click to select a native D-pad direction instead of walking.
// Defer a neutral lone click until release, so selecting an item cannot jump.
// After a direction is used, require centre before restoring locomotion.
class StickDpad {
 public:
  void Reset() { *this = StickDpad{}; }
  bool Apply(VrPadHand& hand, long long now_ms) {
    if (now_ms < last_time_ms_) Reset();
    last_time_ms_ = now_ms;
    const float x = hand.stick_x, y = hand.stick_y;
    const bool finite = std::isfinite(x) && std::isfinite(y);
    const bool neutral = finite && std::hypot(x, y) <= 0.2f;
    if (rearm_) {
      const bool released = !hand.stick_click;
      hand.stick_x = hand.stick_y = 0.0f;
      hand.stick_click = false;
      if (neutral && released) Reset();
      return true;
    }
    if (hand.stick_click) {
      click_until_ms_ = 0;
      held_ = true;
      const bool direction = finite && std::max(std::fabs(x), std::fabs(y)) >= 0.55f;
      used_ = used_ || direction || !finite;
      hand.stick_x = hand.stick_y = 0.0f;
      hand.stick_click = false;
      if (direction) {
        hand.dpad_x = x;
        hand.dpad_y = y;
      }
      return true;
    }
    if (!held_) {
      hand.stick_click = now_ms < click_until_ms_;
      return false;
    }
    hand.stick_click = !used_ && neutral;
    if (hand.stick_click) click_until_ms_ = now_ms + 120;
    hand.stick_x = hand.stick_y = 0.0f;
    rearm_ = !neutral;
    held_ = used_ = false;
    return true;
  }
 private:
  bool held_ = false;
  bool used_ = false;
  bool rearm_ = false;
  long long click_until_ms_ = 0;
  long long last_time_ms_ = 0;
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
  // Physical attack bridge (swing_attack_math.h): a qualified physical swing
  // of the weapon hand. Unlike the deprecated pulse fields these apply even
  // when suppress_weapon_attacks is set -- that is the entire point: the
  // swing replaces the button the player no longer presses. The caller owns
  // the press/release window so the game sees one discrete attack.
  std::uint16_t swing_buttons = 0;
  bool swing_heavy = false;
  // Physical defence bridge (shield_guard_math.h): the shield hand's tracked
  // pose. A raised, still hand holds block (LB); a qualified flick taps parry
  // (LT). The caller owns both press windows, so the
  // two lines are mutually exclusive and apply even when
  // suppress_weapon_attacks is set -- the physical shield replaces the button
  // the player no longer presses.
  bool guard_block = false;
  bool guard_parry = false;
  // Reach-to-interact bridge (reach_intent_math.h): a forward thrust of the
  // free hand requests the native interact button. Until an H1 target owner
  // exists the input.cpp eligibility gate refuses untargeted gestures, so an
  // enabled reach only logs the refusal instead of tapping A at a wall or a
  // face. The caller owns the press window; this is one line held down for
  // that window, not a damage or target claim.
  bool reach_interact = false;
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

// The controller's own D-pad, mapped in gameplay and menu navigation alike.
// Native DSR reads these bits for item/spell selection, equipping and the map,
// so a D-pad that is bound but never read is an invisible missing control.
// A diagonal collapses to one axis (the native D-pad has no diagonal state)
// and a non-finite axis reads as neutral.
inline void ApplyDpad(const VrPadHand& hand, VirtualPadState& out) {
  const float x = std::isfinite(hand.dpad_x) ? hand.dpad_x : 0.0f;
  const float y = std::isfinite(hand.dpad_y) ? hand.dpad_y : 0.0f;
  if (std::fabs(y) >= std::fabs(x)) {
    if (y >= 0.5f) out.buttons |= kPadDpadUp;
    else if (y <= -0.5f) out.buttons |= kPadDpadDown;
  } else if (x >= 0.5f) out.buttons |= kPadDpadRight;
  else if (x <= -0.5f) out.buttons |= kPadDpadLeft;
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
  ApplyDpad(input.left, out);
  ApplyDpad(input.right, out);
  if(menu_navigation) {
    // Native DSR menu lists consume D-pad, rather than locomotion axes. A real
    // D-pad component is the direct channel and wins outright; the stick keeps
    // synthesising the same bits for profiles that expose no D-pad at all.
    if(!(out.buttons & (kPadDpadUp|kPadDpadDown|kPadDpadLeft|kPadDpadRight))) {
      const float x=input.left.stick_x,y=input.left.stick_y;
      if(std::isfinite(x) && std::isfinite(y)) {
        if(std::fabs(y)>=std::fabs(x) && std::fabs(y)>.55f)
          out.buttons|=y>0?kPadDpadUp:kPadDpadDown;
        else if(std::fabs(x)>.55f)out.buttons|=x>0?kPadDpadRight:kPadDpadLeft;
      }
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
  // suppress_weapon_attacks (IK arms on): squeeze/trigger RB/RT/LB/LT all
  // stay zero. The physical arms ARE the attack, block and parry; any canned
  // native animation driven by a controller button would fight the IK solve
  // for the same bones.
  //
  // The opt-in preview requests ordinary native attacks independently of manual
  // button suppression. It does not bind physical hit geometry or damage.
  if (!menu_navigation) {
    out.buttons |= input.swing_buttons;
    if (input.swing_heavy) out.right_trigger = 255;
    // Native DSR controls use distinct channels: LB blocks, LT parries.
    if (input.guard_block) out.buttons |= kPadLeftShoulder;
    if (input.guard_parry) out.left_trigger = 255;
  // The reach gesture taps the same A the player's thumb would press. Which
  // object it lands on, and whether anything answers at all, is the game's.
  if (input.reach_interact) out.buttons |= kPadA;
  }
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
