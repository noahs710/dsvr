#pragma once

// In-hand equipment wheel, decision geometry (lane C follow-up).
//
// Design adapted from ERVR's weapon_wheel_logic.h (GPL-3.0, (c) Ilya
// Mezerowsky): fixed per-slot angles, deadzone with sticky pick, cycle-step
// planning that skips empty slots. Reimplemented here for DSVR's Vec3 and
// project conventions; no ERVR code is copied. Kept free of game and GPU
// state: the caller supplies which slots are filled (DSR's slot-listing
// seam is open; CombatEquipment today carries only the active IDs) and
// turns the pick into the game's own switch presses, like the other
// physical bridges.
//
// DSR carries three weapon slots per hand (series-standard RH1/2/3 +
// LH1/2/3 menu rows); the geometry below is written for three.

#include <cmath>

#include "camera_math.h"

namespace dsvr {

constexpr int kEquipWheelSlots = 3;

// Slot k's direction on the wheel, radians clockwise from straight up:
// slot 0 at the top, slot 1 down and outward (right of the right hand,
// left of the left), slot 2 down and inward. Fixed per slot, never
// repacked around empty ones, so a slot is always the same hand movement.
inline float EquipWheelSlotAngle(int slot, bool right_hand) {
  constexpr float kStep = 2.0943951f;  // 120 degrees
  const float a = slot == 1 ? kStep : slot == 2 ? -kStep : 0.f;
  return right_hand ? a : -a;
}

// The slot the hand is over: the filled slot nearest in angle to the hand's
// offset from the wheel centre (x right, y up, metres in the wheel plane)
// once the hand has left the dead zone, -1 inside it. The current pick holds
// until another slot is nearer by sticky_rad, and the dead zone only takes
// it back at three quarters of its radius, so a hand resting on a border
// does not flicker.
inline int EquipWheelPick(float x, float y, const bool filled[kEquipWheelSlots],
                          bool right_hand, float dead_zone, int current,
                          float sticky_rad = 0.17f) {
  const float dz = current >= 0 ? dead_zone * 0.75f : dead_zone;
  if (x * x + y * y < dz * dz) return -1;
  const float a = std::atan2(x, y);
  int best = -1;
  float best_d = 1e9f, cur_d = 1e9f;
  for (int k = 0; k < kEquipWheelSlots; ++k) {
    if (!filled[k]) continue;
    float d = std::fabs(a - EquipWheelSlotAngle(k, right_hand));
    if (d > 3.14159265f) d = 6.2831853f - d;
    if (d < best_d) {
      best_d = d;
      best = k;
    }
    if (k == current) cur_d = d;
  }
  if (current >= 0 && current < kEquipWheelSlots && filled[current] &&
      best != current && cur_d - best_d < sticky_rad)
    return current;
  return best;
}

// Switch presses the game needs from slot `from` to slot `to`: each one
// moves to the next filled slot in slot order, wrapping (empty slots are
// skipped). 0 = already there, -1 = `to` is empty or unreachable.
inline int EquipWheelCycleSteps(int from, int to,
                                const bool filled[kEquipWheelSlots]) {
  if (to < 0 || to >= kEquipWheelSlots || !filled[to]) return -1;
  if (from == to) return 0;
  int s = from, n = 0;
  for (int i = 0; i < kEquipWheelSlots; ++i) {
    s = (s + 1) % kEquipWheelSlots;
    if (!filled[s]) continue;
    ++n;
    if (s == to) return n;
  }
  return -1;
}

// The filled slot one press before `to`: switching from there lands on `to`
// with a single press of the game's own switch instead of one per slot.
// Deviation from ERVR: an empty `to` is refused outright (-1) rather than
// naming a slot whose press would sail past it.
inline int EquipWheelSlotBefore(int to, const bool filled[kEquipWheelSlots]) {
  if (to < 0 || to >= kEquipWheelSlots || !filled[to]) return -1;
  for (int i = 1; i < kEquipWheelSlots; ++i) {
    const int s = (to - i + kEquipWheelSlots) % kEquipWheelSlots;
    if (filled[s]) return s;
  }
  return -1;
}

}  // namespace dsvr
