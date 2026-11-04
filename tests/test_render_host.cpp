#include "../src/core/render_host.h"

#include <cstdio>
#include <thread>

static int checks = 0, failures = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
  std::printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
using namespace dsvr;

struct Backend {
  RenderHost* host = nullptr;
  int captures = 0, polls = 0, submissions = 0;
  HostStamp last{};
  HostSource source{RenderHostKind::ReShade, 11, 22, 33, 44};
  bool reenter = false, invalidate_during_poll = false, concurrent = false;
  bool invalidate_during_capture = false;
  HostDispatch nested = HostDispatch::Dispatched;
  HostOperations Operations() {
    return {this,
      [](void* p, std::uintptr_t d) {
        auto& b = *static_cast<Backend*>(p); ++b.captures;
        CHECK(d == b.source.device);
        if (b.invalidate_during_capture)
          b.host->Invalidate(b.source.host, b.source.swapchain);
      },
      [](void* p) {
        auto& b = *static_cast<Backend*>(p); ++b.polls;
        if (b.reenter) b.nested = b.host->Dispatch(b.source);
        if (b.concurrent) {
          std::thread t([&b] { b.nested = b.host->Dispatch(b.source); });
          t.join();
        }
        if (b.invalidate_during_poll)
          b.host->Invalidate(b.source.host, b.source.swapchain);
      },
      [](void* p, const HostSource& s, HostStamp stamp) {
        auto& b = *static_cast<Backend*>(p); ++b.submissions; b.last = stamp;
        CHECK(s.image == b.source.image);
      }};
  }
};

int main() {
  {
    RenderHost h; Backend b; b.host = &h;
    CHECK(h.Dispatch(b.source) == HostDispatch::Refused);
    CHECK(!h.Configure(false, RenderHostKind::ReShade, b.Operations()));
    CHECK(!h.Configure(true, RenderHostKind::ReShade, b.Operations()));
    CHECK(h.Dispatch(b.source) == HostDispatch::Refused);
    CHECK(b.captures == 0 && b.polls == 0 && b.submissions == 0);
  }
  {
    RenderHost h; Backend b; b.host = &h;
    CHECK(!h.Configure(true, RenderHostKind::ReShade, {}));
    CHECK(h.Dispatch(b.source) == HostDispatch::Refused);
  }
  for (auto selected : {RenderHostKind::Present, RenderHostKind::ReShade}) {
    RenderHost h; Backend b; b.host = &h; b.source.host = selected;
    CHECK(h.Configure(true, selected, b.Operations()));
    auto s = b.source;
    s.host = selected == RenderHostKind::Present ? RenderHostKind::ReShade
                                                : RenderHostKind::Present;
    CHECK(h.Dispatch(s) == HostDispatch::Refused);
    s = b.source; s.device = 0; CHECK(h.Dispatch(s) == HostDispatch::Refused);
    s = b.source; s.swapchain = 0; CHECK(h.Dispatch(s) == HostDispatch::Refused);
    s = b.source; s.image = 0; CHECK(h.Dispatch(s) == HostDispatch::Refused);
    s = b.source; s.thread = 0; CHECK(h.Dispatch(s) == HostDispatch::Refused);
    CHECK(b.captures == 0 && b.polls == 0 && b.submissions == 0);
    CHECK(h.Dispatch(b.source) == HostDispatch::Dispatched);
    CHECK(b.captures == 1 && b.polls == 1 && b.submissions == 1);
    CHECK(b.last.generation == 1 && b.last.ordinal == 1);
    s = b.source; ++s.thread; CHECK(h.Dispatch(s) == HostDispatch::Refused);
    s = b.source; ++s.swapchain; CHECK(h.Dispatch(s) == HostDispatch::Refused);
    b.reenter = true;
    CHECK(h.Dispatch(b.source) == HostDispatch::Dispatched);
    CHECK(b.nested == HostDispatch::Busy);
    b.reenter = false; b.concurrent = true;
    CHECK(h.Dispatch(b.source) == HostDispatch::Dispatched);
    CHECK(b.nested == HostDispatch::Busy);
    b.concurrent = false;
    h.Invalidate(selected, 999);  // unrelated destruction is not ours
    h.Invalidate(s.host == RenderHostKind::Present ? RenderHostKind::ReShade
                                                 : RenderHostKind::Present, 22);
    CHECK(h.Dispatch(b.source) == HostDispatch::Dispatched);
    CHECK(b.last.generation == 1);
    h.Invalidate(selected, 22);
    b.source.swapchain = 23; b.source.image = 34;
    CHECK(h.Dispatch(b.source) == HostDispatch::Dispatched);
    CHECK(b.last.generation == 2 && b.last.ordinal == 5);
    CHECK(b.captures == 1 && b.polls == 5 && b.submissions == 5);
    // Resize same object still creates a distinct image lifetime.
    h.Invalidate(selected, 23);
    CHECK(h.Dispatch(b.source) == HostDispatch::Dispatched);
    CHECK(b.last.generation == 3 && b.last.ordinal == 6);
    // Replacement device is a permanent refusal, not a second XR session.
    s = b.source; ++s.device;
    CHECK(h.Dispatch(s) == HostDispatch::Refused);
    CHECK(h.Dispatch(b.source) == HostDispatch::Refused);
    CHECK(b.captures == 1 && b.submissions == 6);
  }
  {
    RenderHost h; Backend b; b.host = &h;
    CHECK(h.Configure(true, RenderHostKind::ReShade, b.Operations()));
    b.invalidate_during_capture = true;
    CHECK(h.Dispatch(b.source) == HostDispatch::Refused);
    CHECK(b.captures == 1 && b.polls == 0 && b.submissions == 0);
    b.invalidate_during_capture = false;
    CHECK(h.Dispatch(b.source) == HostDispatch::Refused);
  }
  {
    RenderHost h; Backend b; b.host = &h;
    CHECK(h.Configure(true, RenderHostKind::ReShade, b.Operations()));
    b.invalidate_during_poll = true;
    CHECK(h.Dispatch(b.source) == HostDispatch::Refused);
    CHECK(b.captures == 1 && b.polls == 1 && b.submissions == 0);
    b.invalidate_during_poll = false;
    CHECK(h.Dispatch(b.source) == HostDispatch::Refused);
  }
  {
    // No-mirror window policy: hide only when the mirror is off, VR owns the
    // frame loop, and the headset session is visible. Every other combination
    // keeps the window so the game never loses its only display.
    CHECK(ShouldHideDesktopWindow(false, true, true));
    CHECK(!ShouldHideDesktopWindow(true, true, true));
    CHECK(!ShouldHideDesktopWindow(false, false, true));
    CHECK(!ShouldHideDesktopWindow(false, true, false));
    CHECK(!ShouldHideDesktopWindow(true, false, false));
    CHECK(!ShouldHideDesktopWindow(false, false, false));
    // Minimize companion: mirror off + VR on + session not yet visible.
    CHECK(ShouldMinimizeDesktopWindow(false, true, false));
    CHECK(!ShouldMinimizeDesktopWindow(false, true, true));
    CHECK(!ShouldMinimizeDesktopWindow(true, true, false));
    CHECK(!ShouldMinimizeDesktopWindow(false, false, false));
    // Focus spoof: mirror off + VR on, regardless of session visibility.
    CHECK(ShouldSpoofFocus(false, true));
    CHECK(!ShouldSpoofFocus(true, true));
    CHECK(!ShouldSpoofFocus(false, false));
    CHECK(!ShouldSpoofFocus(true, false));
  }
  std::printf("render host: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
