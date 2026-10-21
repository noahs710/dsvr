#include "../src/core/menu_presentation.h"
#include <cmath>
#include <cstdio>
#include <limits>

int main() {
  int failures = 0;
  auto check = [&](bool ok) { if (!ok) ++failures; };
  auto near = [](float a, float b) { return std::fabs(a - b) < 0.0001f; };
  auto front = dsvr::MakeMenuAnchor(1, 1.65f, 3, 0, 2);
  check(near(front.x, 1));
  check(front.y < 1.65f && front.z < 3);
  const float dy = front.y - 1.65f, dz = front.z - 3;
  check(near(std::sqrt(dy * dy + dz * dz), 2));
  auto side = dsvr::MakeMenuAnchor(1, 1.65f, 3, 1.5707963268f, 2);
  check(side.x < 1 && near(side.z, 3));
  check(near(dsvr::MenuWidth(2, 60), 2.309401f));
  check(dsvr::MenuWidth(2, 66) > dsvr::MenuWidth(2, 60));
  check(near(dsvr::MenuWidth(4, 60), 2 * dsvr::MenuWidth(2, 60)));
  // yaw = pi puts the anchor behind (-Z convention: z increases past origin).
  {
    auto behind = dsvr::MakeMenuAnchor(1, 1.65f, 3, 3.1415926536f, 2);
    check(near(behind.x, 1));
    check(behind.z > 3);
    const float bdy = behind.y - 1.65f, bdz = behind.z - 3;
    check(near(std::sqrt(bdy * bdy + bdz * bdz), 2));
  }
  // yaw = 3*pi/2 mirrors the pi/2 case.
  {
    auto mirror = dsvr::MakeMenuAnchor(1, 1.65f, 3, 4.7123889804f, 2);
    check(mirror.x > 1 && near(mirror.z, 3));
    check(near(mirror.x - 1, 1 - side.x));
  }
  // Controller rays project onto the front of a centered menu quad.
  {
    const dsvr::MenuAnchor anchor{0, 0, 0, 0};
    auto center = dsvr::ProjectMenuRay({0, 0, 2}, {0, 0, -4}, anchor, 2, 2);
    check(center.hit && center.inside);
    check(near(center.u, 0.5f) && near(center.v, 0.5f));

    auto top_right = dsvr::ProjectMenuRay({1, 0.5f, 2}, {0, 0, -1}, anchor, 2, 2);
    check(top_right.hit && top_right.inside);
    check(near(top_right.u, 1) && near(top_right.v, 0));
    auto bottom_left = dsvr::ProjectMenuRay({-1, -0.5f, 2}, {0, 0, -1}, anchor, 2, 2);
    check(bottom_left.hit && bottom_left.inside);
    check(near(bottom_left.u, 0) && near(bottom_left.v, 1));
    auto right_edge = dsvr::ProjectMenuRay({1, 0, 2}, {0, 0, -1}, anchor, 2, 2);
    check(right_edge.hit && right_edge.inside && near(right_edge.u, 1));
    auto outside = dsvr::ProjectMenuRay({1.1f, 0, 2}, {0, 0, -1}, anchor, 2, 2);
    check(outside.hit && !outside.inside && outside.u > 1);

    const dsvr::MenuAnchor rotated{0, 0, 0, 1.5707963268f};
    auto yaw_center = dsvr::ProjectMenuRay({2, 0, 0}, {-1, 0, 0}, rotated, 2, 2);
    check(yaw_center.hit && yaw_center.inside);
    check(near(yaw_center.u, 0.5f) && near(yaw_center.v, 0.5f));
    auto yaw_edge = dsvr::ProjectMenuRay({2, 0, -1}, {-1, 0, 0}, rotated, 2, 2);
    check(yaw_edge.hit && yaw_edge.inside && near(yaw_edge.u, 1));

    auto back_facing = dsvr::ProjectMenuRay({0, 0, -2}, {0, 0, 1}, anchor, 2, 2);
    check(!back_facing.hit && !back_facing.inside);
    auto parallel = dsvr::ProjectMenuRay({0, 0, 2}, {1, 0, 0}, anchor, 2, 2);
    check(!parallel.hit && !parallel.inside);
    auto behind_ray = dsvr::ProjectMenuRay({0, 0, 2}, {0, 0, 1}, anchor, 2, 2);
    check(!behind_ray.hit && !behind_ray.inside);

    auto bad_width = dsvr::ProjectMenuRay({0, 0, 2}, {0, 0, -1}, anchor, 0, 2);
    auto bad_aspect = dsvr::ProjectMenuRay({0, 0, 2}, {0, 0, -1}, anchor, 2, 0);
    auto zero_direction = dsvr::ProjectMenuRay({0, 0, 2}, {0, 0, 0}, anchor, 2, 2);
    auto bad_origin = dsvr::ProjectMenuRay(
        {std::numeric_limits<float>::infinity(), 0, 2}, {0, 0, -1}, anchor, 2, 2);
    auto bad_yaw = dsvr::ProjectMenuRay({0, 0, 2}, {0, 0, -1},
        {0, 0, 0, std::numeric_limits<float>::quiet_NaN()}, 2, 2);
    check(!bad_width.hit && !bad_aspect.hit && !zero_direction.hit);
    check(!bad_origin.hit && !bad_yaw.hit);
    check(!bad_width.inside && !bad_aspect.inside && !zero_direction.inside);
  }
  {
    const dsvr::MenuAnchor anchor{0, 0, 0, 0};
    const auto pointer = dsvr::ProjectMenuPointer(true, true, true, true,
        {0, 0, 2}, {0, 0, -1}, anchor, 2, 2, 1280, 720);
    check(pointer.spatial_menu && pointer.valid && pointer.select);
    check(near(pointer.x, 640) && near(pointer.y, 360));
    check(dsvr::MenuPointerMouseDown(pointer));
    check(!dsvr::MenuGamepadSelect(pointer, true));

    const auto stale = dsvr::ProjectMenuPointer(true, true, false, true,
        {0, 0, 2}, {0, 0, -1}, anchor, 2, 2, 1280, 720);
    const auto unanchored = dsvr::ProjectMenuPointer(true, false, true, true,
        {0, 0, 2}, {0, 0, -1}, anchor, 2, 2, 1280, 720);
    const auto outside = dsvr::ProjectMenuPointer(true, true, true, true,
        {2, 0, 2}, {0, 0, -1}, anchor, 2, 2, 1280, 720);
    check(stale.spatial_menu && !stale.valid && !stale.select);
    check(!unanchored.valid && !unanchored.select);
    check(!outside.valid && !outside.select);
    check(!dsvr::MenuPointerMouseDown(stale));
    check(!dsvr::MenuGamepadSelect(stale, true));
    const auto desktop = dsvr::ProjectMenuPointer(false, true, true, true,
        {0, 0, 2}, {0, 0, -1}, anchor, 2, 2, 1280, 720);
    check(!desktop.valid && !dsvr::MenuPointerMouseDown(desktop));
    check(dsvr::MenuGamepadSelect(desktop, true));
  }
  std::printf("menu geometry: %d failures\n", failures);
  return failures ? 1 : 0;
}
