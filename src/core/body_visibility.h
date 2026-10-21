#pragma once

#include <cstdint>

namespace dsvr {

struct BodyVisibilityStatus {
  bool installed = false;
  std::uint64_t visits = 0;
  std::uint64_t filtered = 0;
  std::uint64_t refused = 0;
  std::uint64_t refreshes = 0;
};

// Excludes the native modular player's generated face/eyes, face, headgear and hair. It does
// not modify skeletal transforms or claim to split mixed arm/body meshes.
bool BodyVisibilityHookInstall(bool enabled);
void BodyVisibilityHookUninstall();
BodyVisibilityStatus BodyVisibilityGetStatus();

}  // namespace dsvr
