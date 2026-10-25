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

// Excludes native modular player geometry by part slot: the generated face/eyes,
// face, headgear and hair, and separately the torso and legs. It does not modify
// skeletal transforms or claim to split mixed arm/body meshes, and the primary
// (weapon) model is never read, so a torso/leg filter keeps arms and weapons
// drawing. Every exclusion is reversible: the live part records are never
// written, only the copy handed to the native queue command is filtered.
bool BodyVisibilityHookInstall(bool enabled);
void BodyVisibilityHookUninstall();
BodyVisibilityStatus BodyVisibilityGetStatus();

// Asks the native updater for a fresh parts command for the local player now.
// A mod-menu visibility toggle calls this so the change lands on the next
// native update instead of waiting for the frame updater to notice that the
// mask changed on a model it already knows. False means no binding is available
// to refresh, not that the setting failed to apply.
bool BodyVisibilityRequestRefresh();

}  // namespace dsvr
