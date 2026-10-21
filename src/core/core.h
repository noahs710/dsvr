#pragma once

namespace dsvr {

// Called only by an accepted frame from the startup-selected render host.
// Acquires gameplay adapters once, after render installation has returned.
void CoreOnRenderFrame();

// Called only after a real successful desktop Present. Grants blur readiness
// without publishing a render host or enabling XR/gameplay adapters.
void CoreOnDesktopFrame();

}  // namespace dsvr
