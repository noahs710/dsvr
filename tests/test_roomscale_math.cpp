#include "../src/core/pad_math.h"
#include <cmath>
#include <cstdio>

int main() {
  int failures = 0;
  auto check = [&](bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
  };

  const auto still = dsvr::RoomscaleAxesFromVelocity(0.08f, 0.0f);
  check(!still.active && still.x == 0.0f && still.y == 0.0f,
        "roomscale deadzone does not generate locomotion");
  const auto walk = dsvr::RoomscaleAxesFromVelocity(0.0f, 0.6f);
  check(walk.active && walk.x == 0.0f && walk.y > 0.0f && walk.y < 1.0f,
        "forward roomscale walking maps to a partial forward stick");
  const auto diagonal = dsvr::RoomscaleAxesFromVelocity(0.6f, 0.6f);
  check(diagonal.active && diagonal.x > 0.0f && diagonal.y > 0.0f &&
        std::fabs(diagonal.x - diagonal.y) < 1e-5f,
        "roomscale diagonal preserves its direction");
  const auto fast = dsvr::RoomscaleAxesFromVelocity(0.0f, 3.0f);
  check(fast.active && fast.y == 1.0f,
        "roomscale speed saturates at full analog intent");
  const auto invalid = dsvr::RoomscaleAxesFromVelocity(NAN, 1.0f);
  check(!invalid.active && invalid.x == 0.0f && invalid.y == 0.0f,
        "invalid roomscale velocity fails closed");

  dsvr::VrPadInput input{};
  input.roomscale_x = 0.25f;
  input.roomscale_y = -0.5f;
  const auto pad = dsvr::MapVrPad(input);
  check(pad.thumb_lx > 8190 && pad.thumb_ly < -16380,
        "roomscale intent reaches native XInput locomotion axes");
  input.left.stick_x = -0.25f;
  const auto combined = dsvr::MapVrPad(input);
  check(combined.thumb_lx == 0 && combined.thumb_ly < -16380,
        "roomscale intent combines with controller locomotion");
  input.left.stick_x = 0.0f;
  const auto menu = dsvr::MapVrPad(input, true);
  check(menu.thumb_lx == 0 && menu.thumb_ly == 0,
        "roomscale intent never steers native menu navigation");
  if (failures) return 1;
  std::puts("roomscale_math tests passed");
  return 0;
}
