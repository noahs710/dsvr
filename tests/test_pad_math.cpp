#include <cmath>
#include <iostream>

#include "../src/core/pad_math.h"

int main() {
  int failures = 0;
  auto check = [&](bool ok, const char* message) {
    if (!ok) {
      std::cerr << "FAIL: " << message << "\n";
      ++failures;
    }
  };

  dsvr::VrPadInput in{};
  in.left.stick_x = -1.0f;
  in.left.stick_y = 0.5f;
  in.left.primary = true;
  in.left.secondary = true;
  in.left.menu = true;
  in.left.stick_click = true;
  in.right.primary = true;
  in.right.secondary = true;
  in.right.stick_click = true;
  in.left.squeeze = 1.0f;
  in.right.trigger = 1.0f;

  const dsvr::VirtualPadState pad = dsvr::MapVrPad(in);
  check(pad.thumb_lx == -32768, "left stick -1 maps to XInput minimum");
  check(pad.thumb_ly >= 16383 && pad.thumb_ly <= 16384,
        "left stick 0.5 maps to half-scale");
  check(pad.thumb_rx == 0 && pad.thumb_ry == 0,
        "right stick is reserved for VR turning");
  check((pad.buttons & dsvr::kPadX) != 0, "left primary maps to X");
  check((pad.buttons & dsvr::kPadY) != 0, "left secondary maps to Y");
  check((pad.buttons & dsvr::kPadA) != 0, "right primary maps to A");
  check((pad.buttons & dsvr::kPadB) != 0, "right secondary maps to B");
  check((pad.buttons & dsvr::kPadStart) != 0,
        "controller menu opens the game menu");
  check((pad.buttons & dsvr::kPadLeftThumb) != 0,
        "left stick click maps to L3");
  check((pad.buttons & dsvr::kPadRightThumb) != 0,
        "right stick click maps to R3");
  // Right-handed default: weapon (right) squeeze = RB, trigger = RT;
  // off-hand (left, squeezed in this fixture) = LB.
  check((pad.buttons & dsvr::kPadRightShoulder) == 0,
        "weapon squeeze released does not light-attack");
  check((pad.buttons & dsvr::kPadLeftShoulder) != 0,
        "off-hand squeeze maps to LB (block)");
  check(pad.right_trigger == 255, "weapon trigger maps to RT analogue");
  check(pad.left_trigger == 0, "off-hand trigger released stays 0");

  // Dominant hand mirrors the combat buttons.
  dsvr::VrPadInput lefty{};
  lefty.dominant_left = true;
  lefty.left.squeeze = 1.0f;
  lefty.left.trigger = 0.5f;
  lefty.right.squeeze = 1.0f;
  const dsvr::VirtualPadState lpad = dsvr::MapVrPad(lefty);
  check((lpad.buttons & dsvr::kPadRightShoulder) != 0 &&
            (lpad.buttons & dsvr::kPadLeftShoulder) != 0,
        "left-handed: both squeezes map, weapon side is RB");
  check(lpad.right_trigger >= 127 && lpad.right_trigger <= 128,
        "left-handed weapon trigger is the left controller trigger");
  check(lpad.left_trigger == 0, "left-handed off-hand trigger released");

  dsvr::VrPadInput bad{};
  bad.left.stick_x = NAN;
  bad.left.stick_y = 2.0f;
  const dsvr::VirtualPadState safe = dsvr::MapVrPad(bad);
  check(safe.thumb_lx == 0, "non-finite axes fail closed");
  check(safe.thumb_ly == 32767, "axes clamp to XInput range");

  dsvr::VirtualPadState physical{};
  physical.buttons = dsvr::kPadRightShoulder;
  physical.left_trigger = 120;
  physical.right_trigger = 240;
  physical.thumb_lx = 111;
  physical.thumb_ly = -222;
  physical.thumb_rx = 333;
  physical.thumb_ry = -444;
  dsvr::VirtualPadState motion{};
  motion.buttons = dsvr::kPadA;
  motion.thumb_lx = -1000;
  motion.thumb_ly = 2000;
  const dsvr::VirtualPadState merged =
      dsvr::OverlayMotionPad(physical, motion);
  check((merged.buttons & dsvr::kPadRightShoulder) != 0 &&
            (merged.buttons & dsvr::kPadA) != 0,
        "motion buttons merge with a physical pad");
  check(merged.thumb_lx == -1000 && merged.thumb_ly == 2000,
        "motion controller owns locomotion, including centred values");
  check(merged.thumb_rx == 333 && merged.thumb_ry == -444,
        "physical right stick remains available");
  check(merged.left_trigger == 120 && merged.right_trigger == 240,
        "reserved motion triggers do not erase physical triggers");

  // Motion-swing pulse: slash injects RB, thrust injects full RT.
  dsvr::VrPadInput swing{};
  swing.pulse_buttons = dsvr::kPadRightShoulder;
  check((dsvr::MapVrPad(swing).buttons & dsvr::kPadRightShoulder) != 0,
        "slash pulse maps to RB");
  dsvr::VrPadInput stab{};
  stab.pulse_heavy = true;
  check(dsvr::MapVrPad(stab).right_trigger == 255,
        "thrust pulse maps to full RT");

  // IK arms on: ALL canned combat outputs are nullified. The physical
  // arms are the attack, block and parry; any canned RB/RT/LB/LT would
  // double-drive the same bones the IK solver owns.
  dsvr::VrPadInput ik{};
  ik.right.squeeze = 1.0f;
  ik.right.trigger = 1.0f;
  ik.left.squeeze = 1.0f;
  ik.left.trigger = 1.0f;
  ik.suppress_weapon_attacks = true;
    const dsvr::VirtualPadState ikpad = dsvr::MapVrPad(ik);
  check((ikpad.buttons & dsvr::kPadRightShoulder) == 0,
          "IK: weapon squeeze does not fire RB");
    check(ikpad.right_trigger == 0, "IK: weapon trigger does not fire RT");
  check((ikpad.buttons & dsvr::kPadLeftShoulder) == 0,
        "IK: off-hand squeeze does not fire LB (physical guard instead)");
  check(ikpad.left_trigger == 0, "IK: off-hand trigger does not fire LT");

  // The physical attack bridge is deliberately outside that suppression: with
  // the IK body applied the swing IS the attack input, and the IK still owns
  // the arm chain, so the press rides the physical blade, not an animation.
  ik.swing_buttons = dsvr::kPadRightShoulder;
  ik.swing_heavy = true;
  const dsvr::VirtualPadState swung = dsvr::MapVrPad(ik);
  check((swung.buttons & dsvr::kPadRightShoulder) != 0,
        "IK: a physical swing still fires the light attack");
  check(swung.right_trigger == 255, "IK: a strong swing still drives RT");

  // The physical guard rides the same suppression: the shield hand replaces the
  // buttons the player no longer presses: LB blocks and LT parries.
  ik.guard_block = true;
  const dsvr::VirtualPadState guarded = dsvr::MapVrPad(ik);
  check(guarded.left_trigger == 0, "IK: holding a guard must not issue parry LT");
  check((guarded.buttons & dsvr::kPadLeftShoulder) != 0,
        "IK: a raised shield holds native block LB");
  ik.guard_block = false;
  ik.guard_parry = true;
  const dsvr::VirtualPadState tapped = dsvr::MapVrPad(ik);
  check(tapped.left_trigger == 255, "IK: a shield flick taps native parry LT");
  check((tapped.buttons & dsvr::kPadLeftShoulder) == 0,
        "IK: a parry does not also hold block LB");
  ik.guard_parry = false;
  check(dsvr::MapVrPad(ik).left_trigger == 0,
        "IK: a dropped guard or finished parry releases LT again");

  ik.guard_block = true;
  const auto menu = dsvr::MapVrPad(ik, true);
    check((menu.buttons & dsvr::kPadRightShoulder) == 0 && menu.right_trigger == 0,
          "native menu navigation cannot consume a swing attack preview");
    check(menu.left_trigger == 0,
          "native menu navigation cannot consume a physical guard");
  {
    dsvr::StickDpad routing;
    long long time_ms = 1000;
    const auto feed = [&](float x, float y, bool click) {
      dsvr::VrPadHand hand{};
      hand.stick_x = x;
      hand.stick_y = y;
      hand.stick_click = click;
      routing.Apply(hand, time_ms);
      time_ms += 16;
      dsvr::VrPadInput sample{};
      sample.left = hand;
      return dsvr::MapVrPad(sample);
    };
    for (const auto& direction : {dsvr::kPadDpadUp, dsvr::kPadDpadDown,
                                  dsvr::kPadDpadLeft, dsvr::kPadDpadRight}) {
      routing.Reset();
      const float x = direction == dsvr::kPadDpadLeft ? -1.0f :
                      direction == dsvr::kPadDpadRight ? 1.0f : 0.0f;
      const float y = direction == dsvr::kPadDpadDown ? -1.0f :
                      direction == dsvr::kPadDpadUp ? 1.0f : 0.0f;
      const auto selected = feed(x, y, true);
      check(selected.buttons == direction && selected.thumb_lx == 0 &&
            selected.thumb_ly == 0, "all four modifier directions select without walking");
      check(feed(x, y, false).buttons == 0,
            "direction release cannot synthesize a native click");
      const auto blocked = feed(x, y, false);
      check(blocked.thumb_lx == 0 && blocked.thumb_ly == 0,
            "persistent deflection cannot escape neutral rearm");
      feed(0, 0, false);
      const auto walking = feed(x, y, false);
      check(walking.thumb_lx != 0 || walking.thumb_ly != 0,
            "new stick motion resumes only after neutral");
    }
    routing.Reset();
    check(feed(0, 0.54f, true).buttons == 0, "D-pad selection has a deadzone");
    check(feed(0, 0.55f, true).buttons == dsvr::kPadDpadUp,
          "D-pad enters at its explicit threshold");
    check(feed(0.8f, 0.8f, true).buttons == dsvr::kPadDpadUp,
          "diagonal selection resolves to one deterministic direction");
    routing.Reset();
    check(feed(0, 0, true).buttons == 0, "neutral click is deferred");
    check(feed(0, 0, false).buttons == dsvr::kPadLeftThumb,
          "neutral click preserves native L3 on release");
    check(feed(0, 0, false).buttons == dsvr::kPadLeftThumb,
          "L3 persists long enough for slower native input polls");
    time_ms += 120;
    check(feed(0, 0, false).buttons == 0, "L3 releases after its bounded hold");
    routing.Reset();
    check(feed(NAN, 1, true).buttons == 0, "invalid direction fails closed");
    check(feed(0, 0, false).buttons == 0, "invalid selection cannot become L3");
    feed(0, 0, true);
    routing.Reset();
    check(feed(0, 0, false).buttons == 0, "lifecycle reset discards deferred clicks");
  }
  if (failures) return 1;
  std::cout << "pad_math tests passed\n";
  return 0;
}
