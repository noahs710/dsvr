#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "arm_pose_math.h"

namespace dsvr {

// Read-only verification of the pinned native 140207F10 copy contract.
// Native code alone applies this mapping/proportion operation. This verifier
// must never be used as a replacement body writer or an avatar-scale setter.
// Static bytes and the independent natural capture: docs/ARM_IK_EVIDENCE.md.
inline constexpr unsigned kPoseCopyCapacity = 80;
struct PoseCopyLayout {
  unsigned source_count = 0, destination_count = 0;
  bool proportions = false;
  std::array<short, kPoseCopyCapacity> destination{};
  std::array<std::uint8_t, kPoseCopyCapacity * 4> records{};
  std::array<float, 5> values{};
  bool operator==(const PoseCopyLayout&) const = default;
};

inline bool PoseCopyScale(const PoseCopyLayout& copy, unsigned destination,
                          std::array<float, 3>& scale) {
  scale = {1, 1, 1};
  if (destination >= copy.destination_count || destination >= kPoseCopyCapacity)
    return false;
  if (!copy.proportions) return true;
  const auto* record = copy.records.data() + destination * 4;
  const unsigned group = record[0] >> 4;
  if (group >= copy.values.size()) return true;  // native no-proportion sentinel
  const float value = copy.values[group];
  if (!std::isfinite(value)) return false;
  const int negative = static_cast<std::int8_t>(record[1]);
  const int positive = static_cast<std::int8_t>(record[2]);
  const float factor = static_cast<float>(value < 0 ? -negative : positive) *
                       value * .01f + 1.f;
  if (!std::isfinite(factor) || factor <= 0.f) return false;
  for (unsigned axis = 0; axis < 3; ++axis)
    if (record[0] & (2u << axis)) scale[axis] = factor;
  return true;
}

inline bool PoseCopyValid(const PoseCopyLayout& copy,
                          std::span<const bool> owned) {
  if (!copy.source_count || copy.source_count > kPoseCopyCapacity ||
      !copy.destination_count || copy.destination_count > kPoseCopyCapacity ||
      owned.size() != kPoseCopyCapacity) return false;
  for (float value : copy.values) if (!std::isfinite(value)) return false;
  std::array<bool, kPoseCopyCapacity> used{};
  for (unsigned source = 0; source < kPoseCopyCapacity; ++source) {
    if (source >= copy.source_count) {
      if (owned[source]) return false;
      continue;
    }
    const int destination = copy.destination[source];
    if (destination == -1) {
      if (owned[source]) return false;
      continue;
    }
    if (destination < 0 || static_cast<unsigned>(destination) >= copy.destination_count ||
        used[destination]) return false;
    used[destination] = true;
    std::array<float, 3> scale{};
    if (!PoseCopyScale(copy, destination, scale)) return false;
  }
  return true;
}

inline bool PoseCopyMatches(const PoseCopyLayout& copy,
                            std::span<const ArmPoseMatrix> source,
                            std::span<const ArmPoseMatrix> body,
                            std::span<const bool> owned) {
  if (!PoseCopyValid(copy, owned) || source.size() < copy.source_count ||
      body.size() < copy.destination_count) return false;
  for (unsigned bone = 0; bone < copy.source_count; ++bone) {
    if (!owned[bone]) continue;
    const unsigned destination = static_cast<unsigned>(copy.destination[bone]);
    std::array<float, 3> scale{};
    if (!PoseCopyScale(copy, destination, scale)) return false;
    for (unsigned element = 0; element < 12; ++element) {
      const unsigned column = element % 4;
      const float expected = source[bone].m[element] * (column < 3 ? scale[column] : 1.f);
      const float observed = body[destination].m[element];
      if (!std::isfinite(expected) || !std::isfinite(observed) ||
          std::fabs(observed - expected) > 2e-5f * std::max(1.f, std::fabs(expected)))
        return false;
    }
  }
  return true;
}

}  // namespace dsvr
