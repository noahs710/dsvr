#pragma once
#include <cstdint>
namespace dsvr {
struct NativeUiStatus {bool installed=false;std::uint64_t visits=0,hidden=0;};
// Pinned native 2D draw-command interpreter; simulation/UI input processing is
// outside this function. DSVR's own renderer bypasses it.
bool NativeUiHookInstall(bool enabled);
void NativeUiHookUninstall();
NativeUiStatus NativeUiGetStatus();
}
