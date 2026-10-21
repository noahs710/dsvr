#include "lab_runner.h"

#include <windows.h>

#include <atomic>
#include <mutex>

#include "../common/logging.h"

namespace dsvr {
namespace {

enum class Request { None, Start, Abort };
std::atomic<Request> g_request{Request::None};
std::atomic<bool> g_watch{false}, g_input_owned{false}, g_contended{false};
std::atomic<bool> g_enabled{false};
std::atomic<std::uint64_t> g_owned_capture{0};
std::mutex g_mutex;
LabScenario g_scenario;
LabSafety g_safety{};
LabObservation g_last{};
std::uint64_t g_observations = 0;
bool g_capture_owned = false;  // diagnostic writer only
LabPhase g_reported_phase = LabPhase::Dormant;
std::uint64_t g_reported_run = 0;
DWORD g_user_input_stamp = 0;

void Publish() {
  g_watch.store(g_scenario.Active());
  g_input_owned.store(g_scenario.OwnsInput());
}

void Trace(const char* kind, const LabResult& result, const LabObservation& o,
           const VirtualPadState* pad = nullptr) {
  VrTraceEvent e{};
  e.event = kind;
  e.seq = result.run;  // lab-run identity, not a tracking packet
  e.epoch = o.scene_epoch;
  e.object = o.player;
  e.frame = o.sequence;
  e.flags = static_cast<unsigned>(result.phase);
  e.values[0] = o.root.x; e.values[1] = o.root.y; e.values[2] = o.root.z;
  e.values[3] = o.camera.x; e.values[4] = o.camera.y; e.values[5] = o.camera.z;
  if (pad) { e.values[6] = pad->thumb_lx; e.values[7] = pad->thumb_ly; }
  for (unsigned i = 0; i < 3; ++i) {
    e.values[8+i] = static_cast<float>(result.polls[i]);
    e.values[11+i] = static_cast<float>(result.observations[i]);
  }
  e.values[14] = result.walk_distance;
  e.values[15] = result.stop_drift;
  VrTraceRecord(e);
}

}  // namespace

void LabInitialize(bool pinned_executable) {
  std::lock_guard<std::mutex> lock(g_mutex);
#ifdef DSVR_ENABLE_LAB
  g_safety.enabled = true;
#endif
  g_safety.pinned_executable = pinned_executable;
  // Launch allowance, save isolation and offline state are NOT inferred from
  // offline_only_ack or a test command. Their verified live adapters come next.
  g_enabled.store(g_safety.enabled);
}

bool LabRequestStart() {
  auto expected = Request::None;
  return g_request.compare_exchange_strong(expected, Request::Start);
}

void LabRequestAbort() { g_request.store(Request::Abort); }

bool LabWantsObservation() {
  return g_enabled.load() && (g_watch.load() || g_request.load() == Request::Start);
}

void LabObserveGame(LabObservation o) {
  if (!LabWantsObservation()) return;
  std::unique_lock<std::mutex> lock(g_mutex, std::try_to_lock);
  if (!lock.owns_lock()) { g_contended.store(true); return; }
  o.sequence = ++g_observations;
  g_last = o;
  const auto before = g_scenario.Result().phase;
  const auto capture_id = g_owned_capture.load();
  if (capture_id) {
    if (capture_id != VrTraceCaptureId()) g_scenario.Abort(LabReason::CaptureFailed);
    else g_scenario.Observe(g_safety, o, GetTickCount64(), VrTraceState());
  }
  const auto result = g_scenario.Result();
  Trace(result.phase == before ? "lab_observation" : "lab_phase", result, o);
  Publish();
}

bool LabPollPad(const VirtualPadState& physical, const VirtualPadState& motion,
                VirtualPadState& out) {
  if (!g_input_owned.load()) return false;
  std::unique_lock<std::mutex> lock(g_mutex, std::try_to_lock);
  if (!lock.owns_lock()) {
    out = {};  // contention can never retain the last walking command
    g_contended.store(true);
    return true;
  }
  const bool owned = g_scenario.Poll(physical, motion, GetTickCount64(), out);
  if (owned) Trace("lab_pad_served", g_scenario.Result(), g_last, &out);
  Publish();
  return owned;
}

void LabPump() {
  std::unique_lock<std::mutex> lock(g_mutex);  // writer only; hooks try-lock
  const auto now = GetTickCount64();
  const auto request = g_request.exchange(Request::None);
  if (request == Request::Abort) g_scenario.Abort(LabReason::RequestedAbort);
  if (g_capture_owned && g_owned_capture.load() != VrTraceCaptureId()) {
    g_scenario.Abort(LabReason::CaptureFailed);
    g_capture_owned = false;  // never stop/finish the replacement capture
    g_owned_capture.store(0);
  }
  if (g_contended.exchange(false)) g_scenario.Abort(LabReason::Contended);
  if (request == Request::Start && !g_scenario.Active()) {
    LASTINPUTINFO info{sizeof(LASTINPUTINFO), 0};
    if (GetLastInputInfo(&info)) g_user_input_stamp = info.dwTime;
    else g_last.user_intervened = true;
  }
  bool begin_capture = false;
  if (request == Request::Start && g_scenario.Start(g_safety, g_last, now)) {
    // Never borrow or close a manually started F9 capture.
    if (VrTraceState() == TraceCaptureState::Recording ||
        VrTraceState() == TraceCaptureState::Closing) {
      g_scenario.Abort(LabReason::CaptureFailed);
    } else begin_capture = true;
  }
  // An old capture's terminal state does not belong to this new request.
  // Observe its capture lifecycle only after our own Begin has succeeded.
  if (!begin_capture && g_scenario.Active())
    g_scenario.Observe(g_safety, g_last, now, VrTraceState());
  if (g_scenario.Active()) {
    LASTINPUTINFO info{sizeof(LASTINPUTINFO), 0};
    if (!GetLastInputInfo(&info)) g_scenario.Abort(LabReason::InvalidObservation);
    else if (info.dwTime != g_user_input_stamp) g_scenario.Abort(LabReason::UserIntervened);
  }
  const auto result = g_scenario.Result();
  const bool stop_capture = g_capture_owned && result.phase == LabPhase::AwaitClose;
  const bool finish_capture = g_capture_owned && !g_scenario.Active();
  const bool report = result.run != g_reported_run || result.phase != g_reported_phase;
  g_reported_run = result.run;
  g_reported_phase = result.phase;
  Publish();
  lock.unlock();  // never hold the scenario lock across files or ordinary logs
  if (begin_capture) {
    g_capture_owned = VrTraceBegin();
    g_owned_capture.store(g_capture_owned ? VrTraceCaptureId() : 0);
    if (!g_capture_owned) {
      lock.lock();
      g_scenario.Abort(LabReason::CaptureFailed);
      Publish();
      lock.unlock();
    }
  }
  if (stop_capture) VrTraceRequestStop();
  if (finish_capture) VrTraceFinish(result.phase != LabPhase::Passed);
  if (g_capture_owned) {
    const auto state = VrTraceState();
    if (state != TraceCaptureState::Recording && state != TraceCaptureState::Closing) {
      g_capture_owned = false;
      g_owned_capture.store(0);
    }
  }
  if (report) {
    DSVR_LOG("LabResult: %s", LabResultJson(result).c_str());
  }
}

LabResult LabStatus() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_scenario.Result();
}

}  // namespace dsvr
