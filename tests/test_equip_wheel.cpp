// In-hand equipment wheel decisions (lane C follow-up). No game, no XR:
// pure geometry over caller-supplied fill states. ERVR-derived design,
// reimplemented (see equip_wheel_math.h header note).
#include <cmath>
#include <cstdio>

#include "../src/core/equip_wheel_math.h"

#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main() {
  using namespace dsvr;
  // ---- fixed slot geometry, mirrored per hand ----
  CHECK(std::fabs(EquipWheelSlotAngle(0, true)) < 1e-6f);
  CHECK(std::fabs(EquipWheelSlotAngle(0, false)) < 1e-6f);
  CHECK(EquipWheelSlotAngle(1, true) > 0 && EquipWheelSlotAngle(1, false) < 0);
  CHECK(EquipWheelSlotAngle(2, true) < 0 && EquipWheelSlotAngle(2, false) > 0);
  CHECK(std::fabs(std::fabs(EquipWheelSlotAngle(1, true)) - 2.0943951f) < 1e-6f);
  // ---- dead zone and nearest-filled pick ----
  {
    const bool filled[3] = {true, true, true};
    CHECK(EquipWheelPick(0.01f, 0.01f, filled, true, 0.05f, -1) == -1);
    CHECK(EquipWheelPick(0.0f, 0.3f, filled, true, 0.05f, -1) == 0);  // top
    CHECK(EquipWheelPick(0.3f, -0.2f, filled, true, 0.05f, -1) == 1);  // out
    CHECK(EquipWheelPick(-0.3f, -0.2f, filled, true, 0.05f, -1) == 2);  // in
    // Empty slots never win, however near.
    const bool one[3] = {false, true, false};
    CHECK(EquipWheelPick(0.0f, 0.3f, one, true, 0.05f, -1) == 1);
    CHECK(EquipWheelPick(0.0f, 0.3f, one, false, 0.05f, -1) == 1);
  }
  // ---- stickiness: a border rest does not flicker; a clear move switches --
  {
    const bool filled[3] = {true, true, true};
    const int held = EquipWheelPick(0.05f, 0.3f, filled, true, 0.05f, 0);
    CHECK(held == 0);
    // Retake needs three quarters of the dead zone, not the full radius.
    CHECK(EquipWheelPick(0.02f, 0.02f, filled, true, 0.05f, 0) == -1);
    CHECK(EquipWheelPick(0.0f, 0.04f, filled, true, 0.05f, 0) == 0);
  }
  // ---- cycle planning skips empties and wraps ----
  {
    const bool sparse[3] = {true, false, true};
    CHECK(EquipWheelCycleSteps(0, 0, sparse) == 0);
    CHECK(EquipWheelCycleSteps(0, 2, sparse) == 1);  // empty 1 skipped
    CHECK(EquipWheelCycleSteps(2, 0, sparse) == 1);  // wraps
    CHECK(EquipWheelCycleSteps(0, 1, sparse) == -1);  // empty target refused
    const bool none[3] = {false, false, false};
    CHECK(EquipWheelCycleSteps(0, 2, none) == -1);
    // Single-press landing: the filled slot just before the target.
    CHECK(EquipWheelSlotBefore(2, sparse) == 0);
    CHECK(EquipWheelSlotBefore(0, sparse) == 2);
    CHECK(EquipWheelSlotBefore(1, sparse) == -1);
  }
  std::puts("test_equip_wheel: PASS");
  return 0;
}
