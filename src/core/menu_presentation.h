#pragma once
#include <cmath>
#include "camera_math.h"

namespace dsvr {
// Rendering-independent geometry, reusable when UI extraction lands.
struct MenuAnchor {
  float x, y, z, yaw;
};
inline MenuAnchor MakeMenuAnchor(float x, float y, float z, float yaw,
                                 float distance) {
  constexpr float down = 0.0872664626f;
  return {x - std::sin(yaw) * distance * std::cos(down),
          y - distance * std::sin(down),
          z - std::cos(yaw) * distance * std::cos(down), yaw};
}
inline float MenuWidth(float distance, float degrees) {
  return 2.0f * distance * std::tan(degrees * 0.00872664626f);
}

struct MenuRayProjection {
  bool hit;
  bool inside;
  float u;
  float v;
};

struct MenuPointerState {
  bool spatial_menu = false;
  bool valid = false;
  bool select = false;
  float x = 0.0f;
  float y = 0.0f;
};

inline bool MenuFiniteVec3(Vec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

// Projects a ray onto the front-facing menu quad. source_aspect_ratio is
// source width / source height, so the quad height is width / aspect ratio.
// Texture V follows the usual top-down image convention.
inline MenuRayProjection ProjectMenuRay(Vec3 ray_origin, Vec3 ray_direction,
                                        const MenuAnchor &anchor, float width,
                                        float source_aspect_ratio) {
  const MenuRayProjection miss{false, false, 0.0f, 0.0f};
  if (!MenuFiniteVec3(ray_origin) || !MenuFiniteVec3(ray_direction) ||
      !std::isfinite(anchor.x) || !std::isfinite(anchor.y) ||
      !std::isfinite(anchor.z) || !std::isfinite(anchor.yaw) ||
      !std::isfinite(width) || !std::isfinite(source_aspect_ratio) ||
      width <= 0.0f || source_aspect_ratio <= 0.0f)
    return miss;

  const float direction_length = VLen(ray_direction);
  if (!std::isfinite(direction_length) || direction_length <= 1e-6f)
    return miss;
  const Vec3 direction = VScale(ray_direction, 1.0f / direction_length);
  const float height = width / source_aspect_ratio;
  if (!MenuFiniteVec3(direction) || !std::isfinite(height) || height <= 0.0f)
    return miss;

  const float sin_yaw = std::sin(anchor.yaw);
  const float cos_yaw = std::cos(anchor.yaw);
  const Vec3 normal{sin_yaw, 0.0f, cos_yaw};
  const Vec3 right{cos_yaw, 0.0f, -sin_yaw};
  const float denominator = VDot(direction, normal);
  // The plane's normal faces the player; reject back faces and parallel rays.
  if (!std::isfinite(denominator) || denominator >= -1e-6f) return miss;

  const Vec3 to_plane{anchor.x - ray_origin.x, anchor.y - ray_origin.y,
                      anchor.z - ray_origin.z};
  const float distance = VDot(to_plane, normal) / denominator;
  if (!std::isfinite(distance) || distance < 0.0f) return miss;

  const Vec3 intersection = VAdd(ray_origin, VScale(direction, distance));
  const Vec3 relative{intersection.x - anchor.x, intersection.y - anchor.y,
                      intersection.z - anchor.z};
  const float u = 0.5f + VDot(relative, right) / width;
  const float v = 0.5f - relative.y / height;
  if (!MenuFiniteVec3(intersection) || !MenuFiniteVec3(relative) ||
      !std::isfinite(u) || !std::isfinite(v))
    return miss;

  const bool inside = u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f;
  return {true, inside, u, v};
}

// A spatial pointer is fail-closed unless the quad is anchored and the hand
// pose is fresh/valid. UV maps directly to the texture's top-left pixel grid.
inline MenuPointerState ProjectMenuPointer(bool spatial_menu, bool anchored,
    bool tracking_valid, bool select_down, Vec3 ray_origin, Vec3 ray_direction,
    const MenuAnchor& anchor, float width, float source_aspect_ratio,
    unsigned display_width, unsigned display_height) {
  MenuPointerState state{};
  state.spatial_menu = spatial_menu;
  if (!spatial_menu || !anchored || !tracking_valid ||
      !display_width || !display_height)
    return state;
  const auto ray = ProjectMenuRay(ray_origin, ray_direction, anchor, width,
                                  source_aspect_ratio);
  if (!ray.hit || !ray.inside) return state;
  state.valid = true;
  state.select = select_down;
  state.x = ray.u * static_cast<float>(display_width);
  state.y = ray.v * static_cast<float>(display_height);
  return state;
}

inline bool MenuPointerMouseDown(const MenuPointerState& pointer) {
  return pointer.spatial_menu && pointer.valid && pointer.select;
}
inline bool MenuGamepadSelect(const MenuPointerState& pointer,
                              bool controller_select) {
  return !pointer.spatial_menu && controller_select;
}
}  // namespace dsvr
