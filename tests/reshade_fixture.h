#pragma once
#include "../src/core/render_host.h"

// Test-only ABI. This header/export family is never used by the real core.
struct ReShadeFixtureSnapshot {
  unsigned captures = 0, polls = 0, submissions = 0, readbacks = 0;
  dsvr::HostStamp stamp{};
  std::uintptr_t device = 0, image = 0, swapchain = 0;
  unsigned long thread = 0;
  unsigned pixel = 0, width = 0, height = 0;
  dsvr::HostDispatch result = dsvr::HostDispatch::Refused;
};
