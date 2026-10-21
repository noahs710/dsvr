#pragma once

#include <windows.h>
#include <openxr/openxr.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>

namespace dsvr {

// SYNCHRONIZED sessions still require complete frame cycles, but a runtime may
// throttle both WaitFrame and EndFrame. Keep those empty cycles off the game's
// render thread. The caller retains the session until PauseAndIdle() succeeds.
// No graphics context, swapchain, input or tracking API is used by this worker.
class XrInactiveFramePump {
 public:
  struct Api {
    PFN_xrWaitFrame wait = nullptr;
    PFN_xrBeginFrame begin = nullptr;
    PFN_xrEndFrame end = nullptr;
  };
  enum class Stage { Idle, Wait, Begin, End };
  struct Status {
    XrSession session = XR_NULL_HANDLE;
    bool requested = false, busy = false, stopped = false, faulted = false;
    Stage stage = Stage::Idle;
    std::uint64_t cycles = 0, empty_frames = 0, errors = 0;
    XrResult wait_result = XR_SUCCESS, begin_result = XR_SUCCESS,
             end_result = XR_SUCCESS;
    XrTime predicted_time = 0;
    XrDuration period = 0;
    double wait_ms = 0, end_ms = 0;
    DWORD initialization_error = ERROR_SUCCESS;
  };

  explicit XrInactiveFramePump(Api api) : shared_(std::make_shared<Shared>(api)) {}
  ~XrInactiveFramePump() {
    RequestStop();
    // Closing a thread handle does not wait for or terminate the thread. Its
    // shared state stays alive until the final in-flight frame has completed.
    if (worker_) CloseHandle(worker_);
  }
  XrInactiveFramePump(const XrInactiveFramePump&) = delete;
  XrInactiveFramePump& operator=(const XrInactiveFramePump&) = delete;

  // Render-thread owner only. Repeated requests for the same session are
  // idempotent. A different session can be installed only while fully idle.
  bool Request(XrSession session) {
    auto& state = *shared_;
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!session || state.status.stopped ||
        !state.api.wait || !state.api.begin || !state.api.end) return false;
    if (state.status.busy && state.status.session != session) return false;
    if (state.status.faulted && state.status.session == session) return false;
    if (!worker_ && !StartWorkerLocked()) return false;
    if (state.status.session != session) {
      state.status.session = session;
      state.status.faulted = false;
    }
    state.status.requested = true;
    SetEvent(state.wake);
    return true;
  }

  // Never waits for an XR call. After true is returned, no new worker cycle
  // can begin until Request() is called again. The caller may then render,
  // end the session or release its resources without concurrent frame calls.
  bool PauseAndIdle() {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    shared_->status.requested = false;
    return !shared_->status.busy;
  }
  Status Snapshot() const {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    return shared_->status;
  }
  // A successful native BeginSession may reuse the same handle after a fault.
  // Only the render owner calls this, after exclusive quiescence is established.
  bool ResetSession(XrSession session) {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    auto& status=shared_->status;
    if(status.busy || status.stopped || !session)return false;
    status.session=session;status.requested=false;status.faulted=false;
    status.wait_result=status.begin_result=status.end_result=XR_SUCCESS;
    status.predicted_time=0;status.period=0;
    return true;
  }
  void RequestStop() {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    shared_->status.requested = false;
    shared_->status.stopped = true;
    if (shared_->wake) SetEvent(shared_->wake);
  }
  // Explicit test/tool cleanup, never call from DllMain or a render callback.
  bool StopAndWait(DWORD timeout_ms) {
    RequestStop();
    return !worker_ || WaitForSingleObject(worker_, timeout_ms) == WAIT_OBJECT_0;
  }

 private:
  struct Shared {
    explicit Shared(Api functions) : api(functions) {}
    ~Shared() { if (wake) CloseHandle(wake); }
    Api api;
    std::mutex mutex;
    HANDLE wake = nullptr;
    Status status;
  };
  using Clock = std::chrono::steady_clock;
  static double ElapsedMs(Clock::time_point before) {
    return std::chrono::duration<double, std::milli>(Clock::now() - before).count();
  }
  bool StartWorkerLocked() {
    auto& state = *shared_;
    // The mod is process-lifetime. Pin the code before an asynchronous call
    // can outlive its owner; no unload or loader-lock join can race the worker.
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&WorkerEntry), &module)) {
      state.status.initialization_error = GetLastError(); return false;
    }
    if (!state.wake) state.wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!state.wake) {
      state.status.initialization_error = GetLastError(); return false;
    }
    auto* argument = new (std::nothrow) std::shared_ptr<Shared>(shared_);
    if (!argument) {
      state.status.initialization_error = ERROR_NOT_ENOUGH_MEMORY; return false;
    }
    worker_ = CreateThread(nullptr, 0, &WorkerEntry, argument, 0, nullptr);
    if (!worker_) {
      state.status.initialization_error = GetLastError(); delete argument;
      return false;
    }
    state.status.initialization_error = ERROR_SUCCESS;
    return true;
  }
  static DWORD WINAPI WorkerEntry(void* argument) {
    auto shared = *static_cast<std::shared_ptr<Shared>*>(argument);
    delete static_cast<std::shared_ptr<Shared>*>(argument);
    for (;;) {
      WaitForSingleObject(shared->wake, INFINITE);
      for (;;) {
        XrSession session = XR_NULL_HANDLE;
        {
          std::lock_guard<std::mutex> lock(shared->mutex);
          auto& status = shared->status;
          if (status.stopped) return 0;
          if (!status.requested) break;
          status.busy = true; status.stage = Stage::Wait;
          session = status.session;
        }
        XrFrameWaitInfo wait_info{XR_TYPE_FRAME_WAIT_INFO};
        XrFrameState frame{XR_TYPE_FRAME_STATE};
        const auto wait_start = Clock::now();
        const XrResult waited = shared->api.wait(session, &wait_info, &frame);
        const double wait_ms = ElapsedMs(wait_start);
        XrResult begun = XR_SUCCESS, ended = XR_SUCCESS;
        double end_ms = 0;
        if (XR_SUCCEEDED(waited)) {
          {
            std::lock_guard<std::mutex> lock(shared->mutex);
            shared->status.stage = Stage::Begin;
          }
          XrFrameBeginInfo begin_info{XR_TYPE_FRAME_BEGIN_INFO};
          begun = shared->api.begin(session, &begin_info);
          if (XR_SUCCEEDED(begun)) {
            {
              std::lock_guard<std::mutex> lock(shared->mutex);
              shared->status.stage = Stage::End;
            }
            XrFrameEndInfo end_info{XR_TYPE_FRAME_END_INFO};
            end_info.displayTime = frame.predictedDisplayTime;
            end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
            const auto end_start = Clock::now();
            ended = shared->api.end(session, &end_info);
            end_ms = ElapsedMs(end_start);
          }
        }
        {
          std::lock_guard<std::mutex> lock(shared->mutex);
          auto& status = shared->status;
          status.wait_result = waited; status.begin_result = begun;
          status.end_result = ended; status.wait_ms = wait_ms;
          status.end_ms = end_ms; status.predicted_time = frame.predictedDisplayTime;
          status.period = frame.predictedDisplayPeriod;
          ++status.cycles;
          if (XR_FAILED(waited) || XR_FAILED(begun) || XR_FAILED(ended)) {
            ++status.errors; status.faulted = true; status.requested = false;
          } else ++status.empty_frames;
          status.busy = false; status.stage = Stage::Idle;
          if (status.stopped) return 0;
          if (!status.requested) break;
        }
      }
    }
  }
  std::shared_ptr<Shared> shared_;
  HANDLE worker_ = nullptr;
};

}  // namespace dsvr
