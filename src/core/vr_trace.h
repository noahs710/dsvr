#pragma once

#include <cstdint>

namespace dsvr {

// Diagnostic observations only. Command production is NOT view consumption.
// Native combat detail (LAB builds) is a five-record atomic batch:
// native_combat_contact + native_combat_identity + native_combat_motion +
// native_combat_diagnostics + native_combat_context. Older builds omit motion
// and context. combat_context_schema=1 requires a context for every contact.
// Context values 0..11 are six signed dwords as little-endian 16-bit limbs:
// attack type, behavior ID, selected AtkParam ID, raw table selector, geometry
// selector, weapon ID. Values 12..13 are raw payload +C8/+C9 flag bytes; they
// are NOT the serialized AtkParam +7E byte. Flags bit 0 gates the tuple read,
// bit 1 the flag bytes, bit 2 the event handle (+140, limbs in values 14..15).
// This establishes observation, never an outcome or validated pool lifetime.
// Diagnostics flags hold source-before/target-before/source-after/target-after
// read-result nibbles; duration is read/assembly cost, excluding native/queue/I/O.
// Motion flags are direction-read, velocity-read, direction-finite,
// direction-unit-length and velocity-finite; values carry the raw caller-built
// record floats, which one caller's decompile predicts and no build proves.
// combat_bench_provisional is pre-copy synthetic geometry, never hit evidence.
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
// Diagnostic-writer-only one-shot control. If path exists it is deleted and
// the active bounded capture is asked to close; never call from a game hook.
bool VrTraceConsumeStopRequest(const wchar_t* path);
TraceCaptureState VrTraceState();
// Process-local generation of the last successfully begun capture. A client
// must compare ownership before interpreting state or stopping a capture.
std::uint64_t VrTraceCaptureId();
// Writer-thread abort/shutdown. Pump again if state is still Closing.
void VrTraceFinish(bool interrupted);
// Byte-gated observation hooks, never retire limiter waits or change matrices.
// Includes CPU HGCameraEntity command acceptance; that is NOT a final GPU view.
bool VrTraceInstall(bool enabled);
// Startup-only, LAB-gated contact + native Step observation. No renderer/camera
// hooks or unrelated events; incompatible install domains require a restart.
bool VrTraceInstallCombat();

}  // namespace dsvr
