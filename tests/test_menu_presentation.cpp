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
  // A fixed 16:9 surface contains source images without stretching. Pointer
  // rays in the matte area are ignored and the image edges map back to source.
  {
    float u = 0, v = 0;
    const float wide_source = 3440.0f / 1440.0f;
    constexpr float surface_aspect = 16.0f / 9.0f;
    check(!dsvr::FitSurfaceUvToContent(.5f, .08f, surface_aspect,
                                      wide_source, u, v));
    check(dsvr::FitSurfaceUvToContent(.25f, .5f, surface_aspect,
                                      wide_source, u, v));
    check(near(u, .25f) && near(v, .5f));
    check(dsvr::FitSurfaceUvToContent(.5f, .25f, surface_aspect,
                                      wide_source, u, v));
    const float visible_height = surface_aspect / wide_source;
    check(near(v, (.25f - (1.0f - visible_height) * .5f) / visible_height));
    const float narrow_source = 1280.0f / 1024.0f;
    check(!dsvr::FitSurfaceUvToContent(.05f, .5f, surface_aspect,
                                      narrow_source, u, v));
    check(dsvr::FitSurfaceUvToContent(.5f, .5f, surface_aspect,
                                      narrow_source, u, v));
    check(near(u, .5f) && near(v, .5f));
    dsvr::MenuPointerState pointer{};
    pointer.valid = pointer.select = pointer.beam_valid = true;
    pointer.x = 1720; pointer.y = 80;
    pointer.beam_x = 1720; pointer.beam_y = 720;
    dsvr::MapSurfacePointerToContent(pointer, 3440, 1440, surface_aspect);
    check(!pointer.valid && !pointer.select && pointer.beam_valid);
    pointer.valid = pointer.select = true;
    pointer.x = 1720; pointer.y = 720;
    dsvr::MapSurfacePointerToContent(pointer, 3440, 1440, surface_aspect);
    check(pointer.valid && pointer.select && near(pointer.x, 1720) &&
          near(pointer.y, 720));
  }
  // Horizon anchor: the merged theater panel sits at eye level straight ahead
  // and never tilts with the head. The offset is purely horizontal, the caller
  // supplied y passes through untouched, and the anchor faces back (yaw-only).
  {
    const float yaw = 0.7f, dist = 2.5f;
    auto horizon = dsvr::MakeHorizonAnchor(1, 1.42f, -3, yaw, dist);
    check(near(horizon.y, 1.42f));  // eye level, not the 5-degree down-tilt
    check(near(horizon.x - 1, -std::sin(yaw) * dist));
    check(near(horizon.z + 3, -std::cos(yaw) * dist));
    check(near(std::hypot(horizon.x - 1, horizon.z + 3), dist));
    check(near(horizon.yaw, yaw));
  }
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
        {0, 0, 3}, {0, 0, 2}, {0, 0, -1}, anchor, 2, 2, 1280, 720);
    check(pointer.spatial_menu && pointer.valid && pointer.select);
    check(near(pointer.x, 640) && near(pointer.y, 360));
    check(dsvr::MenuPointerMouseDown(pointer));
    check(!dsvr::MenuGamepadSelect(pointer, false));
    check(dsvr::MenuGamepadSelect(pointer, true));

    // The beam tail is where the hand APPEARS on the panel, seen from the head,
    // not where it casts a shadow on the panel. A hand out to the right and
    // above the centre, closer than the panel, projects well outside the quad
    // and clamps to its far upper corner.
    const auto angled = dsvr::ProjectMenuPointer(true, true, true, false,
        {0, 0, 3}, {0.5f, 0.2f, 2}, {-0.5f, -0.2f, -2}, anchor, 2, 2, 1280, 720);
    check(angled.valid && near(angled.x, 640) && near(angled.y, 360));
    check(angled.beam_valid && std::fabs(angled.beam_x - 1254.4f) < 0.5f &&
          std::fabs(angled.beam_y - 14.4f) < 0.5f);
    // A hand far outside the panel's angular extent clamps just inside its edge,
    // so the beam still leaves the panel toward the hand instead of running off
    // the texture or collapsing onto the cursor.
    const auto wide = dsvr::ProjectMenuPointer(true, true, true, false,
        {0, 0, 3}, {3, 5, 2}, {-3, -5, -2}, anchor, 2, 2, 1280, 720);
    check(wide.valid && wide.beam_valid && std::fabs(wide.beam_x - 1254.4f) < 0.5f &&
          std::fabs(wide.beam_y - 14.4f) < 0.5f);

    // The reported failure: the drawn beam looked pinned to the middle of the
    // screen. A hand held near the head, slightly right and below the eye and
    // much closer than the panel, used to project orthogonally to (736,180) -
    // within a whisker of the panel centre - so the segment read as a line
    // attached to the cursor and anchored at the middle. Seen from the head the
    // same hand sits low and right, and the beam leaves the panel toward it.
    const auto near_head = dsvr::ProjectMenuPointer(true, true, true, false,
        {0, 0, 1.2f}, {0.15f, -0.25f, 0.6f}, {-0.15f, 0.25f, -0.6f},
        anchor, 2, 2, 1280, 720);
    check(near_head.valid && near(near_head.x, 640) && near(near_head.y, 360));
    check(near_head.beam_valid && std::fabs(near_head.beam_x - 832.0f) < 0.5f &&
          std::fabs(near_head.beam_y - 705.6f) < 0.5f);
    check(near_head.beam_y > near_head.y + 100.0f &&
          near_head.beam_x > near_head.x + 100.0f);

    // A hand inside the panel's angular extent gets an unclamped tail, which is
    // what makes the segment read as coming from the controller rather than
    // from the panel's border.
    const auto inside_tail = dsvr::ProjectMenuPointer(true, true, true, false,
        {0, 0, 2}, {0.2f, -0.1f, 1.0f}, {-0.2f, 0.1f, -1.0f},
        anchor, 2, 2, 1280, 720);
    check(inside_tail.valid && near(inside_tail.x, 640) && near(inside_tail.y, 360));
    check(inside_tail.beam_valid && std::fabs(inside_tail.beam_x - 896.0f) < 0.5f &&
          std::fabs(inside_tail.beam_y - 504.0f) < 0.5f);

    // A hand behind the panel plane has no honest tail: no beam, rather than a
    // mirrored one.
    float behind_u = 0, behind_v = 0;
    check(!dsvr::ProjectMenuPointFromView({0, 0, 0}, {0, 0, 2}, anchor, 2, 2,
                                          behind_u, behind_v));
    // The panel normal faces the player, so a ray from behind is a back face:
    // no cursor and no beam, rather than a mirrored one.
    const auto behind = dsvr::ProjectMenuPointer(true, true, true, false,
        {0, 0, 3}, {0, 0, -2}, {0, 0, 1}, anchor, 2, 2, 1280, 720);
    check(!behind.valid && !behind.beam_valid);
    float bad_u = 0, bad_v = 0;
    check(!dsvr::ProjectMenuPoint({std::numeric_limits<float>::quiet_NaN(), 0, 2},
                                  anchor, 2, 2, bad_u, bad_v));
    check(!dsvr::ProjectMenuPoint({0, 0, 2}, anchor, 0, 2, bad_u, bad_v));

    const auto stale = dsvr::ProjectMenuPointer(true, true, false, true,
        {0, 0, 3}, {0, 0, 2}, {0, 0, -1}, anchor, 2, 2, 1280, 720);
    const auto unanchored = dsvr::ProjectMenuPointer(true, false, true, true,
        {0, 0, 3}, {0, 0, 2}, {0, 0, -1}, anchor, 2, 2, 1280, 720);
    const auto outside = dsvr::ProjectMenuPointer(true, true, true, true,
        {0, 0, 3}, {2, 0, 2}, {0, 0, -1}, anchor, 2, 2, 1280, 720);
    check(stale.spatial_menu && !stale.valid && !stale.select);
    check(!unanchored.valid && !unanchored.select);
    check(!outside.valid && !outside.select);
    check(!dsvr::MenuPointerMouseDown(stale));
    check(!dsvr::MenuGamepadSelect(stale, false));
    check(dsvr::MenuGamepadSelect(stale, true));
    const auto desktop = dsvr::ProjectMenuPointer(false, true, true, true,
        {0, 0, 3}, {0, 0, 2}, {0, 0, -1}, anchor, 2, 2, 1280, 720);
    check(!desktop.valid && !dsvr::MenuPointerMouseDown(desktop));
    check(dsvr::MenuGamepadSelect(desktop, true));
  }
  // Pointer motion smoothing: tremor is dropped, a sweep is damped, a
  // deliberate reposition lands on the same millisecond tick, and a gap or a
  // lost ray re-primes instead of interpolating across the panel.
  {
    dsvr::MenuPointerSmoother smooth;
    check(!smooth.Primed());
    smooth.Approach(100.f, 100.f, 0);
    check(smooth.Primed() && near(smooth.X(), 100.f) && near(smooth.Y(), 100.f));
    // Sub-pixel tremor never moves the cursor, so a still hand cannot drag a
    // slider or flip the pointer-mode hysteresis.
    smooth.Approach(100.5f, 100.4f, 11);
    check(near(smooth.X(), 100.f) && near(smooth.Y(), 100.f));
    // A deliberate 10 px move at 90 Hz follows the exponential, not a teleport.
    smooth.Approach(110.f, 100.f, 22);
    const float alpha = 1.f - std::exp(-11.f / dsvr::kMenuPointerTauMs);
    check(std::fabs(smooth.X() - (100.f + 10.f * alpha)) < 1e-3f);
    check(smooth.X() > 100.f && smooth.X() < 110.f);
    // Two frames can share one millisecond tick. The exponential cannot advance
    // without elapsed time, but a flick past the snap distance must still land.
    smooth.Approach(400.f, 100.f, 11);
    check(near(smooth.X(), 400.f));
    // A gap past the filter's budget re-primes rather than sliding across.
    smooth.Approach(410.f, 100.f, 400);
    check(near(smooth.X(), 410.f));
    // A lost ray re-primes: the cursor never slides to wherever the last ray
    // left it when tracking comes back somewhere else.
    smooth.Reset();
    check(!smooth.Primed());
    smooth.Approach(20.f, 20.f, 410);
    check(near(smooth.X(), 20.f) && near(smooth.Y(), 20.f));
    // Non-finite input cannot move or poison the position.
    smooth.Approach(std::numeric_limits<float>::quiet_NaN(), 20.f, 421);
    check(near(smooth.X(), 20.f) && near(smooth.Y(), 20.f));
    smooth.Approach(30.f, std::numeric_limits<float>::infinity(), 432);
    check(near(smooth.X(), 20.f) && near(smooth.Y(), 20.f));
  }
  std::printf("menu geometry: %d failures\n", failures);
  return failures ? 1 : 0;
}
