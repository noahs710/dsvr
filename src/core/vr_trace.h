#pragma once

#include <cstdint>

namespace dsvr {

// Diagnostic observations only. Command production is NOT view consumption.
struct VrTraceEvent {
  const char* event = "unknown";
  long long qpc = 0;
  unsigned long thread = 0;
  std::uint64_t seq = 0;
  std::uint64_t epoch = 0;
  std::uint64_t frame = 0;
  std::uintptr_t object = 0;
  long long predicted_ns = 0;
  long long period_ns = 0;
  long long duration_qpc = 0;
  unsigned flags = 0;
  unsigned native_rate = 0;
  float values[16]{};
  // Admission constraint, not serialized metadata. Zero binds at Record entry.
  std::uint64_t capture_id = 0;
};

// Hooks only enqueue into bounded storage, without waiting on a lock or disk.
// Dropped events are reported; an overflowing capture cannot pass acceptance.
void VrTraceRecord(VrTraceEvent event);
// Raw native input-blocker mask, NOT a safe-scene or gameplay certificate.
// Four 16-bit limbs preserve all 64 bits in the existing float event payload.
void VrTraceRecordInputBlockers(std::uint64_t mask, std::uintptr_t owner);
// Raw natural Step dispatch observation, NOT a native scene epoch or readiness.
struct VrTraceStepSample {
  unsigned kind = 0;  // 1=InGameStayStep, 2=InGameStep, 3=LoadGameStep
  std::uintptr_t owner = 0;
  std::int32_t before = 0, after = 0;
  unsigned state_valid = 0;  // bit 0=before read, bit 1=after read succeeded
  float delta = 0;
  long long duration_qpc = 0;
  // Bind callbacks to their start generation; zero binds at Record entry.
  std::uint64_t capture_id = 0;
};
void VrTraceRecordStep(const VrTraceStepSample&);
bool VrTraceActive();
long long VrTraceClock();
enum class TraceCaptureState {
  Idle, Recording, Closing, Complete, Interrupted, Dropped, IoError
};
// Only the diagnostic writer thread begins/pumps/finishes files. An explicit
// path supports the same production writer in offline tests; never overwrites.
bool VrTraceBegin(const wchar_t* path = nullptr, unsigned duration_ms = 20000);
void VrTracePump();
// Request/state are safe from any thread. Complete is published only AFTER
// drain, checked flush and checked close. It does not certify gameplay/stereo.
void VrTraceRequestStop();
TraceCaptureState VrTraceState();
// Process-local generation of the last successfully begun capture. A client
// must compare ownership before interpreting state or stopping a capture.
std::uint64_t VrTraceCaptureId();
// Writer-thread abort/shutdown. Pump again if state is still Closing.
void VrTraceFinish(bool interrupted);
// Byte-gated observation hooks, never retire limiter waits or change matrices.
// Includes CPU HGCameraEntity command acceptance; that is NOT a final GPU view.
bool VrTraceInstall(bool enabled);

}  // namespace dsvr
