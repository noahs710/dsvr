#pragma once

namespace dsvr {

// Hook the game's imported XInputGetState and overlay the latest OpenXR
// controller snapshot for users 0-3. The hook is pass-through until OpenXR
// has active bound controls, and it preserves a real controller's non-motion
// inputs. No ViGEm driver or virtual-device install is required.
bool XInputBridgeInstall(bool enabled);
void XInputBridgeUninstall();
// Read-only poll watch (test seam): records that the game observed a key
// down through a keyboard-state query. Returns true on the first-ever down
// report, false afterwards; never touches input state.
bool NoteFirstKeyDown(int vkey);

}  // namespace dsvr
