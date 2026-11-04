#pragma once

#include <atomic>
#include <cstdint>

namespace dsvr {

enum class RenderHostKind { Present, ReShade };
enum class HostDispatch { Refused, Busy, Dispatched };

// Borrowed native identities, valid only for the synchronous callback. A frame
// is an observed mono source, NOT a consumed camera or native stereo pair.
struct HostSource {
  RenderHostKind host = RenderHostKind::Present;
  std::uintptr_t device = 0, swapchain = 0, image = 0;
  unsigned long thread = 0;
};
struct HostStamp {
  std::uint64_t generation = 0, ordinal = 0;
};

// No-mirror window policy: hide the desktop game window only when all three
// hold -- the operator turned the mirror off, VR owns the frame loop, and the
// headset session is actually visible. Any other combination keeps the window
// (spectator mirror on, flatscreen VR-off path, or a session that is not
// showing anything yet), so the game can never lose its only display.
inline bool ShouldHideDesktopWindow(bool desktop_present, bool vr_enabled,
                                    bool session_visible) {
  return !desktop_present && vr_enabled && session_visible;
}

// Pre-visible companion: while the mirror is off and VR owns the frame loop
// but the headset session shows nothing yet (startup, loading), the game
// window is minimized instead of hidden. Minimized stays taskbar-recoverable
// if XR never appears; hiding takes over once the session is visible.
inline bool ShouldMinimizeDesktopWindow(bool desktop_present, bool vr_enabled,
                                        bool session_visible) {
  return !desktop_present && vr_enabled && !session_visible;
}

// Headset-only focus: with the mirror off and VR owning the frame loop, the
// game is definitionally the foreground application even though its desktop
// window is hidden or minimized, so games that gate pad input on focus stay
// live. Any other combination reports the real foreground window, leaving
// spectator, flatscreen and desktop flows untouched.
inline bool ShouldSpoofFocus(bool desktop_present, bool vr_enabled) {
  return !desktop_present && vr_enabled;
}
struct HostOperations {
  void* context = nullptr;
  void (*capture_device)(void*, std::uintptr_t) = nullptr;
  void (*poll)(void*) = nullptr;
  void (*submit_mono)(void*, const HostSource&, HostStamp) = nullptr;
};

// Exactly one immutable host choice and one synchronous XR call lane. No waits
// in frame callbacks; reentry and concurrent callers are refused. The device
// and thread remain pinned across resize/replacement because XR owns them.
class RenderHost {
 public:
  bool Configure(bool enabled, RenderHostKind selected, HostOperations ops);
  HostDispatch Dispatch(const HostSource& source);
  void Invalidate(RenderHostKind host, std::uintptr_t swapchain);

 private:
  std::atomic_flag busy_ = ATOMIC_FLAG_INIT;
  std::atomic<bool> faulted_{false};
  bool configured_ = false;
  std::atomic<bool> enabled_{false};
  std::atomic<RenderHostKind> selected_{RenderHostKind::Present};
  HostOperations ops_{};
  std::uintptr_t device_ = 0, swapchain_ = 0;
  unsigned long thread_ = 0;
  std::uint64_t generation_ = 1, ordinal_ = 0;
};

}  // namespace dsvr
