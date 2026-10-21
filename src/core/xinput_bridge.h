#pragma once

namespace dsvr {

// Hook the game's imported XInputGetState and overlay the latest OpenXR
// controller snapshot for user 0. The hook is pass-through until OpenXR has
// active bound controls, and it preserves a real controller's non-motion
// inputs. No ViGEm driver or virtual-device install is required.
bool XInputBridgeInstall(bool enabled);
void XInputBridgeUninstall();

}  // namespace dsvr
