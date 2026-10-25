#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
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
  bool navigation_select = false;  // A selects focus; trigger selects the ray
  // Beam tail in the same texture pixels: the aiming hand's own position
  // projected onto the panel, so the drawn segment points back at the hand.
  bool beam_valid = false;
  float beam_x = 0.0f;
  float beam_y = 0.0f;
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

// Project a world point onto the panel plane along the panel normal, in the
// panel's own u/v, clamped to the quad. The pointer beam's tail uses this: the
// menu is a compositor quad, so the beam is drawn in the panel's texture and
// the projected hand position is what keeps the line aimed at the controller.
inline bool ProjectMenuPoint(Vec3 point, const MenuAnchor& anchor, float width,
                             float source_aspect_ratio, float& u, float& v) {
  const float height = width / source_aspect_ratio;
  if (!MenuFiniteVec3(point) || !std::isfinite(anchor.x) ||
      !std::isfinite(anchor.y) || !std::isfinite(anchor.z) ||
      !std::isfinite(anchor.yaw) || !std::isfinite(width) || width <= 0.0f ||
      !std::isfinite(source_aspect_ratio) || source_aspect_ratio <= 0.0f ||
      !std::isfinite(height) || height <= 0.0f)
    return false;
  const Vec3 right{std::cos(anchor.yaw), 0.0f, -std::sin(anchor.yaw)};
  const Vec3 relative{point.x - anchor.x, point.y - anchor.y,
                      point.z - anchor.z};
  const float projected_u = 0.5f + VDot(relative, right) / width;
  const float projected_v = 0.5f - relative.y / height;
  if (!std::isfinite(projected_u) || !std::isfinite(projected_v)) return false;
  u = std::clamp(projected_u, 0.0f, 1.0f);
  v = std::clamp(projected_v, 0.0f, 1.0f);
  return true;
}

// The beam's tail has to read as the controller's own apparent position on the
// panel, not as a shadow dropped onto it. Orthogonal projection collapses a hand
// held near the head onto the panel centre, which is what made the drawn segment
// look like a line pinned to the middle of the screen. Projecting the hand along
// the head->hand ray lands the tail where the user actually feels the
// controller, so the segment leaves the panel toward the hand and the cursor
// stays exactly where the aim ray put it.
inline bool ProjectMenuPointFromView(Vec3 eye, Vec3 point, const MenuAnchor& anchor,
                                     float width, float source_aspect_ratio,
                                     float& u, float& v) {
  const Vec3 to_hand{point.x - eye.x, point.y - eye.y, point.z - eye.z};
  const float length = VLen(to_hand);
  if (!MenuFiniteVec3(to_hand) || !std::isfinite(length) || length <= 1e-4f)
    return false;
  const auto hit = ProjectMenuRay(eye, to_hand, anchor, width, source_aspect_ratio);
  // A hand behind the panel, or a ray parallel to it, has no honest tail: the
  // beam goes out rather than becoming a mirrored guess.
  if (!hit.hit) return false;
  constexpr float kInset = 0.02f;
  u = std::clamp(hit.u, kInset, 1.0f - kInset);
  v = std::clamp(hit.v, kInset, 1.0f - kInset);
  return true;
}

// A spatial pointer is fail-closed unless the quad is anchored and the hand
// pose is fresh/valid. UV maps directly to the texture's top-left pixel grid.
inline MenuPointerState ProjectMenuPointer(bool spatial_menu, bool anchored,
    bool tracking_valid, bool select_down, Vec3 eye, Vec3 ray_origin,
    Vec3 ray_direction,
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
  float tail_u = 0.0f, tail_v = 0.0f;
  if (ProjectMenuPointFromView(eye, ray_origin, anchor, width,
                                 source_aspect_ratio, tail_u, tail_v)) {
    state.beam_valid = true;
    state.beam_x = tail_u * static_cast<float>(display_width);
    state.beam_y = tail_v * static_cast<float>(display_height);
  }
  return state;
}

// Pointer motion smoothing. The raw ray hit carries hand tremor, controller
// mass and the panel's own anchor drift; ImGui reads all of it as cursor
// chatter and drags widgets the user never meant to move. This is the same
// exponential approach the torso filters use: a sub-pixel
// epsilon drops micromovement, a 45 ms time constant follows a deliberate
// sweep within a frame or two, and a gap or a lost ray re-primes instead of
// interpolating across a teleport. A jump past kMenuPointerSnapPx is a
// deliberate reposition, not jitter, and lands the same frame: damping that
// would smear a wrist flick across the panel also strands the cursor when
// consecutive frames share a millisecond tick. Owner thread only.
inline constexpr float kMenuPointerTauMs = 45.0f;
inline constexpr float kMenuPointerEpsilonPx = 1.2f;
inline constexpr float kMenuPointerSnapPx = 64.0f;
inline constexpr std::uint64_t kMenuPointerGapMs = 200;

class MenuPointerSmoother {
 public:
  void Approach(float target_x, float target_y, std::uint64_t now_ms,
                float tau_ms = kMenuPointerTauMs,
                float epsilon_px = kMenuPointerEpsilonPx) {
    if (!std::isfinite(target_x) || !std::isfinite(target_y) ||
        !std::isfinite(tau_ms) || tau_ms <= 0.0f ||
        !std::isfinite(epsilon_px) || epsilon_px < 0.0f)
      return;
    if (!primed_) {
      primed_ = true;
      x_ = target_x;
      y_ = target_y;
      last_ms_ = now_ms;
      return;
    }
    // The clock advances on every sample, not only on every accepted move, so
    // the gap test measures time since the last sample like the torso filter.
    const std::uint64_t elapsed = now_ms > last_ms_ ? now_ms - last_ms_ : 0;
    if (elapsed) last_ms_ = now_ms;
    const float dx = target_x - x_, dy = target_y - y_;
    const float distance = std::hypot(dx, dy);
    if (distance < epsilon_px) return;
    // A deliberate reposition needs no elapsed time to land: two frames can
    // share one millisecond tick, and a filter that waits for the clock to move
    // strands the cursor on the widget the user already left.
    if (distance > kMenuPointerSnapPx || elapsed > kMenuPointerGapMs) {
      x_ = target_x;
      y_ = target_y;
      return;
    }
    if (!elapsed) return;
    const float alpha = 1.0f - std::exp(-static_cast<float>(elapsed) / tau_ms);
    x_ += dx * alpha;
    y_ += dy * alpha;
  }
  void Reset() {
    primed_ = false;
    x_ = y_ = 0.0f;
    last_ms_ = 0;
  }
  bool Primed() const { return primed_; }
  float X() const { return x_; }
  float Y() const { return y_; }

 private:
  bool primed_ = false;
  float x_ = 0.0f, y_ = 0.0f;
  std::uint64_t last_ms_ = 0;
};

inline bool MenuPointerMouseDown(const MenuPointerState& pointer) {
  return pointer.spatial_menu && pointer.valid && pointer.select;
}
inline bool MenuGamepadSelect(const MenuPointerState& pointer,
                              bool controller_confirm) {
  return controller_confirm || pointer.navigation_select;
}
}  // namespace dsvr
