#pragma once
#include <mutex>
namespace dsvr {
// MinHook's create/enable calls share process-global hook lists and are not
// safe to issue concurrently. Gameplay adapters install from different
// threads (render, camera/game, pose), and run 9 lost the XInput bridge to
// a single create failure amid simultaneous installs. Every adapter takes
// this mutex around its MinHook initialize/create/enable/remove calls.
inline std::mutex& HookInstallMutex() {
  static std::mutex mutex;
  return mutex;
}
}  // namespace dsvr
