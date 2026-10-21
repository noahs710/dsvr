#include "camera_observer.h"

#include <atomic>

#include "../common/memory.h"
#include "vr_trace.h"

namespace dsvr {
namespace {
std::atomic<std::uint64_t> g_samples{0};

void PointerLimbs(std::uintptr_t pointer, float* values) {
  for (unsigned i = 0; i < 4; ++i)
    values[i] = static_cast<float>((pointer >> (16 * i)) & 0xffff);
}
}

void CameraObserveNative(void* follow_cam, std::uint64_t capture_id) {
  if (!follow_cam || !capture_id || capture_id != VrTraceCaptureId() ||
      !VrTraceActive()) return;
  VrTraceEvent e{};
  e.capture_id = capture_id;
  e.seq = ++g_samples;  // sample association, NOT a tracking/simulation sequence
  e.event = "camera_container";
  std::uintptr_t container = 0;
  if (GuardedRead(ModuleBase(nullptr) + 0x1c6e188, container)) {
    e.object = container;
    e.flags = 1;  // global read succeeded (a null container is still observable)
    std::uintptr_t fields[3]{};
    // Factory allocates exactly 0x18; never read old +0x30/+0x70 fields here.
    if (container && GuardedReadBytes(container, fields, sizeof(fields))) {
      e.flags |= 2;
      for (unsigned i = 0; i < 3; ++i) PointerLimbs(fields[i], e.values + 4 * i);
      VrTraceRecord(e);
      for (unsigned i = 0; i < 2; ++i) {
        e.event = "camera_slot";
        e.object = fields[i + 1];
        e.flags = i;  // slot index; neither is labelled active
        for (auto& value : e.values) value = 0;
        std::uintptr_t first_word = 0;
        if (e.object && GuardedRead(e.object, first_word)) {
          e.flags |= 0x100;  // raw first-word read, not a validated vtable
          PointerLimbs(first_word, e.values);
        }
        VrTraceRecord(e);
      }
    } else VrTraceRecord(e);
  } else VrTraceRecord(e);

  e.event = "camera_follow_native";
  e.object = reinterpret_cast<std::uintptr_t>(follow_cam);
  e.flags = 0;
  for (auto& value : e.values) value = 0;
  // Verified committed follow transform, not the unresolved CameraMan object.
  if (GuardedReadBytes(e.object + 0x10, e.values, sizeof(e.values))) e.flags = 1;
  VrTraceRecord(e);
  e.event = "camera_follow_projection";
  e.flags = 0;
  for (auto& value : e.values) value = 0;
  if (GuardedReadBytes(e.object + 0x50, e.values, 2 * sizeof(float))) e.flags = 1;
  VrTraceRecord(e);
}

}  // namespace dsvr
