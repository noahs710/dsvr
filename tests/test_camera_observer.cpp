// Real camera detour + bounded sampler, with recording native/memory seams.
// This proves observer refusals/read bounds, NOT a live game's camera layout.
#include <windows.h>
#include <MinHook.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include "../src/common/logging.h"
#include "../src/common/memory.h"
#include "../src/core/arm_ik.h"
#include "../src/core/camera.h"
#include "../src/core/calibration.h"
#include "../src/core/camera_observer.h"
#include "../src/core/camhook.h"
#include "../src/core/diagnostics.h"
#include "../src/core/input.h"
#include "../src/core/lab_runner.h"
#include "../src/core/vr_trace.h"
#include "../src/core/xr.h"

#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); \
  return 1; } } while (0)

namespace {
using Follow = void(__fastcall*)(void*, float, void*, float*);
Follow detour = nullptr;
constexpr std::uintptr_t base = 0x140000000;
std::uintptr_t container = 0x10000000;
std::uintptr_t fields[3]{0xaaaabbbbccccdddd, 0x20000000, 0x30000000};
std::uintptr_t words[2]{0x123456789abcdef0, 0xfedcba9876543210};
float follow[22]{};
unsigned forbidden = 0, originals = 0, invalid_reads = 0, reads = 0;
unsigned creates = 0, removes = 0;
bool active = true, signature = true, fail_enable = false, replace_capture = false;
bool global_read = true, container_read = true;
bool calibration_test = false, native_head_available = false;
bool manual_eye_height = false;
unsigned camera_applies = 0;
std::uint64_t stature_revision = 9, applied_sequence = 0;
dsvr::CalibrationMapping applied_mapping;
void MutationCall() { if (!calibration_test) ++forbidden; }
std::uint64_t capture = 7;
std::vector<dsvr::VrTraceEvent> events;
void __fastcall Original(void* self, float dt, void* arg3, float* arg4) {
  ++originals;
  if (self != follow || dt != 0.25f || arg3 != &words || arg4 != follow) ++forbidden;
  follow[16] = static_cast<float>(originals);  // the native update owns this edit
  if (replace_capture) ++capture;
}
}

MH_STATUS WINAPI MH_Initialize() { return MH_OK; }
MH_STATUS WINAPI MH_CreateHook(LPVOID target, LPVOID callback, LPVOID* original) {
  ++creates;
  if (target != reinterpret_cast<void*>(base + 0x236c60)) ++forbidden;
  detour = reinterpret_cast<Follow>(callback);
  *original = reinterpret_cast<void*>(&Original);
  return MH_OK;
}
MH_STATUS WINAPI MH_EnableHook(LPVOID) { return fail_enable ? MH_ERROR_MEMORY_PROTECT : MH_OK; }
MH_STATUS WINAPI MH_DisableHook(LPVOID) { return MH_OK; }
MH_STATUS WINAPI MH_RemoveHook(LPVOID) { ++removes; return MH_OK; }

namespace dsvr {
UserCalibration CalibrationCurrent() {
  UserCalibration profile;
  if (calibration_test) {
    profile.valid = true; profile.revision = 17;
    profile.arm_reach_m[0] = profile.arm_reach_m[1] = .6f;
  }
  return profile;
}
void CameraPrepareNativeUpdate(void*) {}
void CameraTrackingSpaceChanged(std::uint64_t) { MutationCall(); }
void LogWrite(LogLevel, const char*, int, const char*, ...) {}
std::uintptr_t ModuleBase(const wchar_t*) { return base; }
bool BytesMatch(std::uintptr_t, const void*, std::size_t) { return signature; }
bool GuardedReadBytes(std::uintptr_t address, void* out, std::size_t size) {
  ++reads;
  if (address == base + 0x1c6e188 && size == sizeof(container)) {
    if (!global_read) return false;
    std::memcpy(out, &container, size); return true;
  }
  if (container && address == container && size == sizeof(fields)) {
    if (!container_read) return false;
    std::memcpy(out, fields, size); return true;
  }
  for (unsigned i = 0; i < 2; ++i) {
    if (fields[i+1] && address == fields[i+1] && size == sizeof(std::uintptr_t)) {
      std::memcpy(out, words + i, size); return true;
    }
  }
  const auto self = reinterpret_cast<std::uintptr_t>(follow);
  if ((address == self + 0x10 && size == 64) ||
      (address == self + 0x50 && size == 8)) {
    std::memcpy(out, reinterpret_cast<void*>(address), size); return true;
  }
  ++invalid_reads;
  return false;
}
void VrTraceRecord(VrTraceEvent e) {
  if (active && (!e.capture_id || e.capture_id == capture)) events.push_back(e);
}
bool VrTraceActive() { return active; }
std::uint64_t VrTraceCaptureId() { return capture; }
Config DiagnosticsCurrentConfig() {
  MutationCall();
  Config c;c.eye_height_override=manual_eye_height;c.eye_height=1.82f;
  c.vr_enabled = c.headlook_enabled = c.xr_input_enabled = c.arm_ik_enabled = true;
  return c;
}
bool LabWantsObservation() { MutationCall(); return !calibration_test; }
void LabObserveGame(LabObservation) { ++forbidden; }
void CameraReadLabObservation(void*, LabObservation&) { ++forbidden; }
void CameraTraceFollowCam(void*) { MutationCall(); }
void CameraResetHeadlook() { MutationCall(); }
void CameraPublishHeadDirectedLocomotion(std::uint64_t) { MutationCall(); }
void InputInvalidateHeadDirectedLocomotion() { MutationCall(); }
bool CameraApplyFirstPerson(void*, float, float, float, float, float, float,
    float, float, float, float, float, float, float, float,
    const CalibrationMapping* mapping) {
  MutationCall(); ++camera_applies;
  if (mapping) applied_mapping = *mapping;
  return true;
}
bool XrMenuPresentationActive() { MutationCall(); return false; }
bool XrLatestTrackingFrame(TrackingFrame& frame) {
  MutationCall();
  frame.head_quat[3] = 1;
  frame.head_pos[1] = 1.65f;
  frame.space_epoch = 3; frame.sequence = 99;
  frame.vfov_rad = 1.4f; frame.fov_aspect = 1.1f;
  return true;
}
void XrRecordCameraSequence(std::uint64_t sequence) { MutationCall(); applied_sequence = sequence; }
float InputTakeTurnYaw() { MutationCall(); return 1; }
void ArmIkTick(const TrackingFrame&) { MutationCall(); }
bool ArmIkNativeReach(float& reach, std::uint64_t& revision) {
  MutationCall(); reach = .54f; revision = 9; return calibration_test;
}
// Same read-only fit query: the observer path must never ask the IK driver for
// native stature either, so it is counted like every other forbidden call.
bool ArmIkNativeStature(float& head, float& shoulders, std::uint64_t& revision) {
  MutationCall();
  head = native_head_available ? 1.8f : 0;
  shoulders = native_head_available ? .42f : 0;
  revision = native_head_available ? stature_revision : 0;
  return calibration_test && native_head_available;
}
void ArmIkHookUninstall() {}
}

int main() {
  using namespace dsvr;
  CHECK(!CameraHookInstall(false, false) && creates == 0);
  signature = false;
  CHECK(!CameraHookInstall(true, true) && creates == 0);
  signature = true;
  fail_enable = true;
  CHECK(!CameraHookInstall(true, true) && creates == 1 && removes == 1);
  fail_enable = false;
  CHECK(CameraHookInstall(true, true) && creates == 2 && detour);
  CHECK(!CameraHookInstall(true, false) && creates == 2);  // no privilege change
  detour(follow, 0.25f, &words, follow);
  CHECK(originals == 1 && forbidden == 0 && invalid_reads == 0);
  CHECK(reads == 6 && events.size() == 7);
  CHECK(std::strcmp(events[0].event, "follow_enter") == 0);
  CHECK(std::strcmp(events[1].event, "follow_exit") == 0);
  CHECK(events[2].object == container && events[2].flags == 3);
  CHECK(events[2].values[0] == 0xdddd && events[2].values[3] == 0xaaaa);
  CHECK(events[3].object == fields[1] && events[3].flags == 0x100);
  CHECK(events[3].values[0] == 0xdef0 && events[3].values[3] == 0x1234);
  CHECK(events[4].object == fields[2] && events[4].flags == 0x101);
  CHECK(events[5].flags == 1 && events[5].values[12] == 1);  // after original
  CHECK(events[6].flags == 1 && events[6].values[2] == 0);
  for (unsigned i = 2; i < events.size(); ++i) {
    CHECK(events[i].seq == events[2].seq && events[i].capture_id == 7);
    CHECK(events[i].epoch == 0 && events[i].frame == 0 && events[i].predicted_ns == 0);
  }
  events.clear();
  const auto old_reads = reads;
  active = false;
  detour(follow, 0.25f, &words, follow);
  CHECK(originals == 2 && reads == old_reads && events.empty() && forbidden == 0);
  active = true;
  replace_capture = true;
  detour(follow, 0.25f, &words, follow);
  CHECK(originals == 3 && reads == old_reads && events.size() == 1);
  replace_capture = false;
  events.clear();
  CameraObserveNative(nullptr, capture);
  CameraObserveNative(follow, capture - 1);
  CameraObserveNative(follow, 0);
  CHECK(reads == old_reads && events.empty());
  global_read = false;
  CameraObserveNative(follow, capture);
  CHECK(events.size() == 3 && events[0].flags == 0 && events[0].object == 0);
  events.clear();
  global_read = true;
  container_read = false;
  CameraObserveNative(follow, capture);
  CHECK(events.size() == 3 && events[0].flags == 1 && events[0].object == container);
  events.clear();
  container_read = true;
  fields[1] = 0;
  CameraObserveNative(follow, capture);
  CHECK(events.size() == 5 && events[1].object == 0 && events[1].flags == 0);
  CHECK(events[2].flags == 0x101 && events[2].object == fields[2]);
  CHECK(forbidden == 0 && invalid_reads == 0);
  CameraHookUninstall();
  // The gameplay detour must retain a valid arm fit while upright native head
  // geometry is unavailable; the two native queries have independent outputs.
  calibration_test = true;
  active = false;
  CHECK(CameraHookInstall(true, false));
  detour(follow, 0.25f, &words, follow);
  CHECK(camera_applies == 1 && applied_sequence == 99);
  CHECK(applied_mapping.valid && applied_mapping.fit == CalibrationFit::Arm &&
        applied_mapping.model_revision == 9);
  native_head_available = true;
  detour(follow, 0.25f, &words, follow);
  CHECK(camera_applies == 2 && applied_mapping.fit == CalibrationFit::Head &&
        applied_mapping.model_revision == 9);
  const auto automatic_scale=applied_mapping.world_scale;
  manual_eye_height=true;
  detour(follow, 0.25f, &words, follow);
  CHECK(camera_applies==3 && applied_mapping.world_scale==automatic_scale);
  CHECK(std::fabs(applied_mapping.eye_height/automatic_scale-1.82f)<1e-5f);
  manual_eye_height=false;
  detour(follow, 0.25f, &words, follow);
  CHECK(camera_applies==4 && std::fabs(applied_mapping.eye_height-1.8f)<1e-5f);
  stature_revision = 10;
  detour(follow, 0.25f, &words, follow);
  CHECK(camera_applies == 4 && applied_sequence == 0);  // refuse mixed rig snapshots
  CameraHookUninstall();
  std::puts("test_camera_observer: PASS (fixture only, native layout unverified)");
  return 0;
}
