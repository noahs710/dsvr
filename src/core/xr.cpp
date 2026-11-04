// P2b OpenXR probe: instance/system/session against the game's D3D11
// device. Runs exclusively on calls from the Present detour (the game's
// render thread), so the DX11 binding is created where it will be used.

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include "xr.h"
#include "d3d11_state_guard.h"

#include <windows.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include "d3d11_source_format.h"
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>

#include "../common/logging.h"
#include "camera.h"
#include "camera_math.h"
#include "stereo_math.h"
#include "calibration.h"
#include "diagnostics.h"
#include "display_rate.h"
#include "input.h"
#include "hand_math.h"
#include "gpu_frame_timer.h"
#include "vr_trace.h"
#include "menu_presentation.h"
#include "menu_state.h"
#include "mod_menu.h"
#include "mono_provenance.h"
#include "depth_probe.h"
#include "xr_eye_resolution.h"
#include "xr_stereo_frame.h"
#include "xr_inactive_frame_pump.h"

namespace dsvr {
namespace {

XrInstance g_instance = XR_NULL_HANDLE;
XrSystemId g_system = XR_NULL_SYSTEM_ID;
XrSession g_session = XR_NULL_HANDLE;
XrSessionState g_state = XR_SESSION_STATE_UNKNOWN;
std::atomic<bool> g_attempted{false};
std::atomic<unsigned long long> g_next_retry_ms{0};
std::atomic<bool> g_running{false};
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
XrSpace g_space = XR_NULL_HANDLE;
XrSpace g_head_space = XR_NULL_HANDLE;
XrSpace g_floor_space = XR_NULL_HANDLE;
bool g_floor_attempted = false;
bool g_local_space = true;
struct EyeResources {
  uint32_t width = 0, height = 0;
  uint32_t recommended_width = 0, recommended_height = 0;
  uint32_t max_width = 0, max_height = 0;
  XrSwapchain swapchain = XR_NULL_HANDLE;
  std::vector<XrSwapchainImageD3D11KHR> images;
  std::vector<ID3D11RenderTargetView*> rtvs;
  ID3D11Texture2D* stage = nullptr;
  ID3D11RenderTargetView* stage_rtv = nullptr;
  ID3D11ShaderResourceView* stage_srv = nullptr;
  ID3D11Texture2D* filtered = nullptr;
  ID3D11RenderTargetView* filtered_rtv = nullptr;
  ID3D11ShaderResourceView* filtered_srv = nullptr;
  uint32_t post_width = 0, post_height = 0;
  DXGI_FORMAT post_format = DXGI_FORMAT_UNKNOWN;
} g_eyes[2];
int64_t g_eye_format = 0;
unsigned long long g_submitted = 0;
int g_eye_scale_percent = 0;  // startup-only; 0 until the first eye setup
// Shader blit (format conversion + aspect-fit). The game renders
// B8G8R8A8_UNORM (87); SteamVR-class runtimes only accept RGBA swapchains,
// so CopyResource cannot be used -- a fullscreen-triangle sample converts.
ID3D11VertexShader* g_blit_vs = nullptr;
ID3D11PixelShader* g_blit_ps = nullptr;
ID3D11SamplerState* g_blit_sampler = nullptr;
ID3D11BlendState* g_blit_blend = nullptr;
ID3D11DepthStencilState* g_blit_depth = nullptr;
ID3D11RasterizerState* g_blit_raster = nullptr;
ID3D11Buffer* g_blit_constants = nullptr;
ID3D11Buffer* g_source_projection_constants = nullptr;
ComfortVignette g_comfort_vignette;
bool g_views_logged = false;
bool g_fov_logged = false;
// Per-eye virtual-screen rect (pixels) for mono mode: the game image is
// drawn only into the part of the eye buffer subtending mono_screen_fov_deg,
// centred on the optical axis (tangent 0), rest black. Drawing full-frame
// (v0.4.0 first success) stretched 16:9 across the native ~100 deg FOV and
// sat off-centre on the asymmetric HMD frustum ("too close, misaligned").
float g_screen_rect[2][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}};
std::atomic<float> g_theater_pointer_fov_rad{0.0f};
// Retained grid resources are teardown-only for sessions created by older
// code; current theater/calibration rooms draw their floor grid in the full
// projection image so final post-processing covers it too.
XrSwapchain g_grid_swapchain = XR_NULL_HANDLE;
std::vector<XrSwapchainImageD3D11KHR> g_grid_images;
std::vector<ID3D11RenderTargetView*> g_grid_rtvs;
uint32_t g_grid_size = 0;
bool g_grid_failed = false;
ID3D11VertexShader* g_grid_vs = nullptr;
ID3D11PixelShader* g_grid_ps = nullptr;
ID3D11Buffer* g_grid_constants = nullptr;
ID3D11BlendState* g_grid_blend = nullptr;
ID3D11PixelShader* g_calibration_room_ps = nullptr;
ID3D11Buffer* g_calibration_room_constants = nullptr;
MenuAnchor g_calibration_anchor{};
bool g_calibration_anchored = false;
float g_calibration_panel_width = 1.2f;
constexpr float kCalibrationPanelDistanceM = 1.35f;
XrPosef g_calibration_stage_pose{{0, 0, 0, 1}, {0, 0, 0}};
float g_calibration_room_half_x = 4.0f;
float g_calibration_room_half_z = 4.0f;
XrVector3f g_calibration_last_eye_position[2]{};
XrQuaternionf g_calibration_last_eye_orientation[2]{};
XrFovf g_calibration_last_eye_fov[2]{};
bool g_calibration_last_eye_fov_valid[2]{};
bool g_calibration_last_eye_position_valid = false;
bool g_calibration_last_view_valid = false;
TrackingFrames g_tracking_frames;
std::mutex g_camera_tag_mutex;
std::uint64_t g_camera_sequence = 0;
DWORD g_camera_thread = 0;
TrackingFrame g_camera_frame;
long long g_last_period_ns = 0;
// Observe the runtime-owned headset refresh for the FPS Unlock dependency's
// next-launch MaxFPS handoff. DSVR does not request or pace this display rate.
PFN_xrGetDisplayRefreshRateFB g_get_display_refresh_rate = nullptr;
std::atomic<double> g_display_refresh_hz = 0.0;

bool ValidDisplayRefresh(float hz) {
  return std::isfinite(hz) && std::isgreaterequal(hz, 10.0f) &&
             std::islessequal(hz, 1000.0f);
}


void QueryDisplayRefresh() {
  g_display_refresh_hz = 0.0;
  if (!g_session || !g_get_display_refresh_rate) return;
  float current_hz = 0.0f;
  const auto result = g_get_display_refresh_rate(g_session, &current_hz);
  if (result != XR_SUCCESS || !ValidDisplayRefresh(current_hz)) {
    DSVR_WARN("XR: physical display refresh unavailable result=%d hz=%.3f; using predicted period",
              static_cast<int>(result), static_cast<double>(current_hz));
    return;
  }
  g_display_refresh_hz = current_hz;
  DSVR_LOG("XR: observing headset refresh %.3fHz; DSR-FPS-Unlock consumes it on next launch",
           static_cast<double>(current_hz));
}
// Effective-rate authority (display_rate.h). Stabilizer is render-thread
// only; the snapshot is mutex-published for XrEffectiveRate readers.
RateStabilizer g_rate_stabilizer;
std::mutex g_rate_mutex;
EffectiveRate g_rate_snapshot{};
std::uint64_t g_rate_logged_generation = 0;
std::mutex g_metrics_mutex;
RenderMetrics g_metrics_snapshot{};
std::uint64_t g_metrics_sequence = 0;
GpuFrameTimer g_gpu_timer;

void PublishRate(long long period_ns) {
  const double display_hz = g_display_refresh_hz.load();
  const EffectiveRate rate = g_rate_stabilizer.Update(period_ns, period_ns > 0 ? display_hz : 0.0);
  g_last_period_ns = period_ns;
  std::lock_guard<std::mutex> lock(g_rate_mutex);
  g_rate_snapshot = rate;
  if (rate.generation != g_rate_logged_generation) {
    g_rate_logged_generation = rate.generation;
    if (rate.from_runtime)
      DSVR_LOG("RateObserve: source=%s period_ns=%lld app=%.3fHz headset=%.3fHz observed=%dHz; "
               "frame limiting delegated to DSR-FPS-Unlock",
               rate.from_display ? "physical-display" :
                   (rate.runtime_throttled ? "runtime-throttled" : "runtime"),
               rate.runtime_period_ns, rate.runtime_hz, rate.display_hz,
               rate.effective_fps);
    else
      DSVR_LOG("RateObserve: no live runtime cadence; DSR-FPS-Unlock owns frame limiting");
  }
}
XrTime g_space_change_time = 0;
bool g_space_change_pending = false;
float g_vfov_rad = 0;
float g_fov_aspect = 0;
bool g_immersive = false;
std::atomic<bool> g_menu_active{false};
// No follow-camera callback is guaranteed on title/login. Keep launch UI in
// theater until natural gameplay is observed; do not interpret later temporary
// sample loss as a menu (it could otherwise turn locomotion into item selection).
std::atomic<bool> g_theater_override{false};
std::atomic<bool> g_recenter_menu{false};
bool g_menu_anchored = false;
MenuAnchor g_menu_anchor{};
// Published meter width of the world-anchored menu surface; the XR pointer
// uses the same width as the room shader.
std::atomic<float> g_panel_width{0.0f};
// Presentation state is debounced (menu_state.h): raw per-frame readings
// (title heuristic / manual override) must hold 150 ms before the canvas
// opens or closes, so menu navigation flicker never flashes immersive
// mode. Render thread only; no lock needed.
MenuDebouncer g_menu_debounce{150};
ID3D11ShaderResourceView* g_blit_srv = nullptr;
ID3D11Texture2D* g_stage = nullptr;  // our copy of the backbuffer, SRV-bindable
uint32_t g_stage_w = 0;
uint32_t g_stage_h = 0;
DXGI_FORMAT g_stage_fmt = DXGI_FORMAT_UNKNOWN;
bool g_stage_from_scene = false;
TrackingFrame g_completed_camera_frame;
unsigned long long g_source_capture_ms = 0;
std::uint64_t g_source_generation = 0;
std::uintptr_t g_last_scene_source = 0;
ID3D11Texture2D* g_menu_surface = nullptr;
ID3D11RenderTargetView* g_menu_surface_rtv = nullptr;
ID3D11ShaderResourceView* g_menu_surface_srv = nullptr;
uint32_t g_menu_surface_w = 0;
uint32_t g_menu_surface_h = 0;
DXGI_FORMAT g_menu_surface_fmt = DXGI_FORMAT_UNKNOWN;
ID3D11PixelShader* g_post_fxaa_ps = nullptr;
ID3D11PixelShader* g_post_cas_ps = nullptr;
ID3D11Buffer* g_post_constants = nullptr;
ID3D11Buffer* g_cas_constants = nullptr;
bool g_format_logged = false;
XrStereoFrame g_native_frame;
XrInactiveFramePump g_inactive_pump({xrWaitFrame,xrBeginFrame,xrEndFrame});
std::atomic<bool> g_session_visible{false};
std::atomic<bool> g_pump_failed{false};
enum class SessionFinish { None, End, Loss };
SessionFinish g_session_finish=SessionFinish::None;
bool g_session_begin_pending=false,g_pump_fault_logged=false;
std::uint64_t g_pump_reported_cycles=0;
struct MonoImageLease {
  XrSwapchain chain = XR_NULL_HANDLE;
  bool ready = false;
} g_mono_images[2];
ID3D11Query* g_destroy_query = nullptr;
bool g_destroy_pending = false;
NativeStereoFrame g_native_prediction{};
DWORD g_xr_thread = 0;
bool g_mono_in_frame = false, g_native_owns_present = false;
std::uint64_t g_mono_image_timeouts=0,g_mono_lease_skips=0;
unsigned long long g_native_begin_ms = 0;
bool DestroySwapchains();

void SampleCalibration(const TrackingFrame& frame) {
  const bool both_triggers = frame.hand[0].valid && frame.hand[0].trigger_held &&
                             frame.hand[1].valid && frame.hand[1].trigger_held &&
                             frame.hand[0].tracking_flags_available &&
                             frame.hand[0].position_tracked && frame.hand[0].orientation_tracked &&
                             frame.hand[1].tracking_flags_available &&
                             frame.hand[1].position_tracked && frame.hand[1].orientation_tracked;
  bool floor_valid = false;
  float floor_height = 0;
  // Sample the judged floor every fresh tracking frame, not only while the
  // calibration screen is visible: eye-height-above-floor is continuous
  // spatial evidence for calibration (height, floor, roomscale) and the
  // merged theater panel anchors on it at any moment.
  if (g_floor_space) {
    XrPosef head{};
    floor_valid = XrStereoFrame::LocateHeadPose(g_head_space, g_floor_space,
                                               frame.predicted_time_ns, head);
    floor_height = floor_valid ? head.position.y : 0;
    floor_valid = floor_valid && std::isfinite(floor_height) && floor_height > .4f &&
                  floor_height < 2.8f;
  }
  const auto profile=CalibrationCurrent();
  // A manual view override is not a new anatomical measurement. Without a
  // floor reference preserve the prior calibration rather than baking it in.
  CalibrationSample(frame, both_triggers, XrSessionVisible(), floor_valid, floor_height,
                     profile.valid ? profile.eye_height_m : Config{}.eye_height,
                     DiagnosticsCurrentConfig().play_style.c_str());
}

bool DrainMonoImages() { return DrainSwapchainLeases(g_mono_images); }

long long QpcNow() {
  LARGE_INTEGER value{};
  QueryPerformanceCounter(&value);
  return value.QuadPart;
}

double QpcElapsedMs(long long ticks) {
  static const long long frequency = [] {
    LARGE_INTEGER value{};
    QueryPerformanceFrequency(&value);
    return value.QuadPart > 0 ? value.QuadPart : 1LL;
  }();
  return (double)ticks * 1000.0 / (double)frequency;
}

void InvalidateTracking(bool new_space = false) {
  g_comfort_vignette.Reset();
  g_tracking_frames.Invalidate(new_space);
  InputInvalidateFrame();
  std::lock_guard<std::mutex> lock(g_camera_tag_mutex);
  g_camera_sequence = 0;
  g_camera_thread = 0;
  g_camera_frame = {};
}

bool Ok(XrResult r, const char* what) {
  if (XR_SUCCEEDED(r)) return true;
  char buf[XR_MAX_RESULT_STRING_SIZE]{};
  if (g_instance)
    xrResultToString(g_instance, r, buf);
  else
    snprintf(buf, sizeof(buf), "result=%d", (int)r);
  DSVR_ERROR("XR: %s failed: %s", what, buf);
  return false;
}

std::string NarrowW(const wchar_t* w) {
  if (!w) return {};
  char buf[512]{};
  WideCharToMultiByte(CP_UTF8, 0, w, -1, buf, sizeof(buf) - 1, nullptr,
                      nullptr);
  return buf;
}

void AdvanceSessionLifecycle() {
  if(g_session_finish!=SessionFinish::None) {
    if(!g_inactive_pump.PauseAndIdle())return;
    g_native_frame.Cancel();
    if(!DestroySwapchains())return;
    if(g_session_finish==SessionFinish::End && g_session &&
        Ok(xrEndSession(g_session),"xrEndSession"))g_native_frame.SessionEnded();
    g_native_prediction={};g_native_owns_present=false;
    g_session_finish=SessionFinish::None;
    DSVR_LOG("XR: deferred session cleanup complete");
  }
  if(g_session_begin_pending && g_session) {
    if(!g_inactive_pump.PauseAndIdle())return;
    XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
    begin.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    g_session_begin_pending=false;
    if(Ok(xrBeginSession(g_session,&begin),"xrBeginSession")) {
      QueryDisplayRefresh();
      g_inactive_pump.ResetSession(g_session);g_pump_fault_logged=false;g_pump_failed.store(false);
      InvalidateTracking(true);g_last_period_ns=0;g_space_change_pending=false;
      g_running.store(true);
      DSVR_LOG("XR: session running; invisible frames use the nonblocking worker");
    }
  }
}

void ServiceInactiveFrames() {
  if(!g_running.load() || !g_session)return;
  const auto previous=g_inactive_pump.Snapshot();
  // A faulted worker refuses new requests until ResetSession clears it (the
  // tested pump contract). Transient wait/begin/end errors therefore self-heal
  // here while the session still runs; only a failed reset suspends VR until
  // the next session begin clears the latch. The worker owns the frame loop
  // only while the session is not renderable, so re-request only there: when
  // VISIBLE/FOCUSED the render thread owns it. Skip the reset while a new
  // cycle is already in flight; it resolves on the next present.
  if(previous.faulted && !previous.busy) {
    const bool inactive=g_state!=XR_SESSION_STATE_VISIBLE &&
        g_state!=XR_SESSION_STATE_FOCUSED;
    bool recovered=g_inactive_pump.ResetSession(g_session);
    if(recovered && inactive)recovered=g_inactive_pump.Request(g_session);
    if(recovered) {
      if(g_pump_failed.load()) {
        g_pump_failed.store(false);
        DSVR_LOG("XR: inactive frame worker recovered after fault; VR resumed");
      }
      g_pump_fault_logged=false;
    } else if(!g_pump_failed.load()) {
      g_pump_failed.store(true);
    }
  }
  if(g_pump_failed.load()) {
    if(!g_pump_fault_logged) {
      g_pump_fault_logged=true;
      DSVR_ERROR("XR: inactive frame cycle failed wait=%d begin=%d end=%d; worker reset failed, VR suspended until session restart",
          static_cast<int>(previous.wait_result),static_cast<int>(previous.begin_result),
          static_cast<int>(previous.end_result));
    }
    return;
  }
  if(g_state==XR_SESSION_STATE_VISIBLE || g_state==XR_SESSION_STATE_FOCUSED)return;
  g_native_frame.Cancel();
  if(g_native_frame.Busy())return;
  if(!g_inactive_pump.Request(g_session) && !g_pump_fault_logged) {
    g_pump_failed.store(true);
    g_pump_fault_logged=true;
    const auto status=g_inactive_pump.Snapshot();
    DSVR_ERROR("XR: inactive frame worker unavailable win32=%lu wait=%d begin=%d end=%d; preserving desktop rendering",
        status.initialization_error,static_cast<int>(status.wait_result),
        static_cast<int>(status.begin_result),static_cast<int>(status.end_result));
  }
  const auto status=g_inactive_pump.Snapshot();
  if(status.cycles!=g_pump_reported_cycles) {
    g_pump_reported_cycles=status.cycles;
    if(status.period>0)PublishRate(status.period);
  }
}

bool GraphicsFrameReady() {
  // The render thread owns the frame loop only while the runtime shows the
  // session (VISIBLE or FOCUSED: shouldRender is true in both). Parking the
  // inactive worker first keeps the two loops mutually exclusive.
  if(!g_running.load() || g_pump_failed.load())return false;
  if(g_state!=XR_SESSION_STATE_VISIBLE && g_state!=XR_SESSION_STATE_FOCUSED)return false;
  if(!g_inactive_pump.PauseAndIdle())return false;
  return !g_inactive_pump.Snapshot().faulted;
}

void PollEvents() {
  if (!g_instance) return;
  for (;;) {
    XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
    XrResult r = xrPollEvent(g_instance, &ev);
    if (r == XR_EVENT_UNAVAILABLE) break;
    if (XR_FAILED(r)) {
      if (r == XR_ERROR_INSTANCE_LOST || r == XR_ERROR_SESSION_LOST) {
        g_display_refresh_hz = 0.0;
        g_running.store(false);
        g_session_begin_pending = false;
        g_session_finish = SessionFinish::Loss;
        InvalidateTracking(true);
        PublishRate(0);
      }
      break;
    }
    if (ev.type == XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB &&
        g_get_display_refresh_rate &&
        g_state != XR_SESSION_STATE_STOPPING &&
        g_state != XR_SESSION_STATE_EXITING &&
        g_state != XR_SESSION_STATE_LOSS_PENDING) {
      const auto* refresh = reinterpret_cast<const XrEventDataDisplayRefreshRateChangedFB*>(&ev);
      g_display_refresh_hz = ValidDisplayRefresh(refresh->toDisplayRefreshRate)
          ? refresh->toDisplayRefreshRate : 0.0;
      // Observe the new panel rate; the dependency is updated for the next launch.
    }
    if (ev.type == XR_TYPE_EVENT_DATA_EVENTS_LOST && g_running.load())
      QueryDisplayRefresh(); // a missing FB event may have changed the cache
    if (ev.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
      g_display_refresh_hz = 0.0;
      g_running.store(false);
      g_session_begin_pending = false;
      g_session_finish = SessionFinish::Loss;
      InvalidateTracking(true);
      PublishRate(0);
    }
    if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
      auto* sc = (XrEventDataSessionStateChanged*)&ev;
      if(sc->session!=g_session)continue;
      g_state = sc->state;
      g_session_visible.store(g_state==XR_SESSION_STATE_VISIBLE || g_state==XR_SESSION_STATE_FOCUSED);
      g_menu_anchored = false;
      g_calibration_anchored = false;
      g_calibration_last_eye_position_valid = false;
      g_calibration_last_view_valid = false;
      g_calibration_last_eye_fov_valid[0] = false;
      g_calibration_last_eye_fov_valid[1] = false;
      DSVR_LOG("XR: session state -> %d", (int)g_state);
      if (g_state == XR_SESSION_STATE_READY && g_session) {
        g_session_begin_pending=true;
      }
      if (g_state == XR_SESSION_STATE_STOPPING && g_session) {
        g_display_refresh_hz = 0.0;
        g_session_begin_pending=false;
        g_session_finish=SessionFinish::End;
        g_inactive_pump.PauseAndIdle();
        InvalidateTracking(true);
        g_running.store(false);
        PublishRate(0);
        DSVR_LOG("XR: session stopping; cleanup waits for inactive frame ownership");
      }
      if (g_state == XR_SESSION_STATE_EXITING ||
          g_state == XR_SESSION_STATE_LOSS_PENDING) {
        g_display_refresh_hz = 0.0;
        g_session_begin_pending=false;g_session_finish=SessionFinish::Loss;
        g_inactive_pump.PauseAndIdle();
        InvalidateTracking(true);
        g_running.store(false);
        PublishRate(0);
        DSVR_LOG("XR: session exiting/loss -- VR will stay flatscreen");
      }
      if(g_state!=XR_SESSION_STATE_FOCUSED)InvalidateTracking();
    }
    if (ev.type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) {
      g_menu_anchored = false;
      g_calibration_anchored = false;
      g_calibration_last_eye_position_valid = false;
      const auto* change = (const XrEventDataReferenceSpaceChangePending*)&ev;
      if (change->referenceSpaceType ==
          (g_local_space ? XR_REFERENCE_SPACE_TYPE_LOCAL : XR_REFERENCE_SPACE_TYPE_VIEW)) {
        g_space_change_time = change->changeTime;
        g_space_change_pending = true;
        InvalidateTracking();
      }
    }
  }
  AdvanceSessionLifecycle();
  g_session_visible.store(g_running.load() &&
      (g_state==XR_SESSION_STATE_VISIBLE || g_state==XR_SESSION_STATE_FOCUSED));
}

bool DestroySwapchains() {
  if(!g_inactive_pump.PauseAndIdle())return false;
  g_native_frame.Cancel();
  if (g_eyes[0].swapchain || g_eyes[1].swapchain) {
    // Teardown only: the XR owner may outlive our queued D3D commands.
    // Keep the same completion marker across bounded cleanup retries and
    // refuse further image use while destruction is pending.
    g_destroy_pending = true;
    if (!g_device || !g_context) return false;
    if (!g_destroy_query) {
      const D3D11_QUERY_DESC query{D3D11_QUERY_EVENT, 0};
      if (FAILED(g_device->CreateQuery(&query, &g_destroy_query))) return false;
      g_context->End(g_destroy_query);
      g_context->Flush();
    }
    const auto deadline = GetTickCount64() + 5;
    for (;;) {
      BOOL complete = FALSE;
      const auto result = g_context->GetData(g_destroy_query, &complete, sizeof(complete),
                                             D3D11_ASYNC_GETDATA_DONOTFLUSH);
      if (result == S_OK && complete) break;
      if (FAILED(result) || GetTickCount64() >= deadline) return false;
      SwitchToThread();
    }
  }
  bool destroyed = true;
  for (int i = 0; i < 2; ++i) {
    if (g_eyes[i].swapchain) {
      if (!Ok(xrDestroySwapchain(g_eyes[i].swapchain), "xrDestroySwapchain")) {
        destroyed = false;
        continue;  // Keep this owner and its images for a later cleanup attempt.
      }
      g_native_frame.SwapchainDestroyed(g_eyes[i].swapchain);
    }
    g_mono_images[i] = {};
    for (auto* rtv : g_eyes[i].rtvs)
      if (rtv) rtv->Release();
    g_eyes[i].rtvs.clear();
    g_eyes[i].swapchain = XR_NULL_HANDLE;
    g_eyes[i].images.clear();
  }
  if (!destroyed) return false;
  if (g_grid_swapchain != XR_NULL_HANDLE) {
    if (!Ok(xrDestroySwapchain(g_grid_swapchain), "xrDestroySwapchain(grid)"))
      return false;
    g_grid_swapchain = XR_NULL_HANDLE;
  }
  for (auto* rtv : g_grid_rtvs)
    if (rtv) rtv->Release();
  g_grid_rtvs.clear();
  g_grid_images.clear();
  g_grid_size = 0;
  g_grid_failed = false;
  if (g_destroy_query) g_destroy_query->Release();
  g_destroy_query = nullptr;
  g_destroy_pending = false;
  for (auto& eye : g_eyes) eye.width = eye.height = 0;
  const auto release = [](auto*& resource) {
    if (resource) resource->Release();
    resource = nullptr;
  };
  release(g_menu_surface_srv);
  release(g_menu_surface_rtv);
  release(g_menu_surface);
  g_menu_surface_w = g_menu_surface_h = 0;
  g_menu_surface_fmt = DXGI_FORMAT_UNKNOWN;
  release(g_blit_srv);
  release(g_stage);
  g_stage_w = g_stage_h = 0;
  g_stage_fmt = DXGI_FORMAT_UNKNOWN;
  g_stage_from_scene = false;
  g_completed_camera_frame = {};
  g_source_capture_ms = 0;
  g_last_scene_source = 0;
  for (auto& eye : g_eyes) {
    release(eye.stage_srv);
    release(eye.stage_rtv);
    release(eye.stage);
    release(eye.filtered_srv);
    release(eye.filtered_rtv);
    release(eye.filtered);
    eye.post_width = eye.post_height = 0;
    eye.post_format = DXGI_FORMAT_UNKNOWN;
  }
  if (g_head_space) xrDestroySpace(g_head_space);
  g_head_space = XR_NULL_HANDLE;
  if (g_floor_space) xrDestroySpace(g_floor_space);
  g_floor_space = XR_NULL_HANDLE;
  g_floor_attempted = false;
  g_native_frame.SwapchainsDestroyed();
  return true;
}

bool EnsureBlit() {
  if (g_blit_vs && g_blit_ps && g_blit_sampler && g_blit_blend &&
      g_blit_depth && g_blit_raster && g_blit_constants &&
        g_source_projection_constants) return true;
  // A failed creation retries cleanly rather than leaking the partial pipeline.
  const auto release = [](auto*& p) { if (p) p->Release(); p = nullptr; };
  release(g_blit_vs); release(g_blit_ps); release(g_blit_sampler);
  release(g_blit_blend); release(g_blit_depth); release(g_blit_raster);
  release(g_blit_constants); release(g_source_projection_constants);
  static const char* kShader =
      "Texture2D tex : register(t0); SamplerState samp : register(s0);"
      "cbuffer Comfort : register(b0) { float4 tangents; float4 vignette; };"
      "cbuffer SourceProjection : register(b1) { float4 source_projection; };"
      "struct PSIn { float4 pos : SV_Position; float2 uv : TEXCOORD0; };"
      "PSIn VSMain(uint id : SV_VertexID) { PSIn o;"
      "  o.uv = float2((id << 1) & 2, id & 2);"
      "  o.pos = float4(o.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);"
      "  return o; }"
      "float Luma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }"
      "float4 PSMain(PSIn i) : SV_Target {"
      "  float2 source_uv=i.uv;"
      "  if(source_projection.z>0.5) {"
      "    float2 ray=float2(lerp(tangents.x,tangents.y,i.uv.x),"
      "      lerp(tangents.z,tangents.w,i.uv.y));"
      "    source_uv=float2(0.5+ray.x/(2*source_projection.x),"
      "      0.5-ray.y/(2*source_projection.y)); }"
      "  float4 c=tex.Sample(samp,source_uv);"
      "  float2 uv = i.pos.xy * vignette.yz;"
      "  float2 t = float2(lerp(tangents.x,tangents.y,uv.x),"
      "                    lerp(tangents.z,tangents.w,uv.y));"
      "  float inner = lerp(1.4,0.55,vignette.x);"
      "  c.rgb *= 1.0-vignette.x*smoothstep(inner,inner+0.35,length(t));"
      "  return float4(c.rgb,1); }";
  ID3DBlob* vs_blob = nullptr;
  ID3DBlob* ps_blob = nullptr;
  ID3DBlob* err = nullptr;
  if (FAILED(D3DCompile(kShader, strlen(kShader), nullptr, nullptr, nullptr,
                        "VSMain", "vs_4_0", 0, 0, &vs_blob, &err)) ||
      FAILED(D3DCompile(kShader, strlen(kShader), nullptr, nullptr, nullptr,
                        "PSMain", "ps_4_0", 0, 0, &ps_blob, &err))) {
    DSVR_ERROR("XR: blit shader compile failed");
    release(vs_blob); release(ps_blob); release(err);
    return false;
  }
  g_device->CreateVertexShader(vs_blob->GetBufferPointer(),
                               vs_blob->GetBufferSize(), nullptr, &g_blit_vs);
  g_device->CreatePixelShader(ps_blob->GetBufferPointer(),
                              ps_blob->GetBufferSize(), nullptr, &g_blit_ps);
  vs_blob->Release();
  ps_blob->Release();
  release(err);
  D3D11_SAMPLER_DESC sd{};
  sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  g_device->CreateSamplerState(&sd, &g_blit_sampler);
  // Full pipeline state: the game's context carries its own blend (possibly
  // colour-write masked), depth, scissor and extra shader stages into our
  // draw. Inheriting any of it can silently produce a black eye image.
  D3D11_BLEND_DESC bd{};
  bd.RenderTarget[0].BlendEnable = FALSE;
  bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  g_device->CreateBlendState(&bd, &g_blit_blend);
  D3D11_DEPTH_STENCIL_DESC dd{};
  dd.DepthEnable = FALSE;
  dd.StencilEnable = FALSE;
  g_device->CreateDepthStencilState(&dd, &g_blit_depth);
  D3D11_RASTERIZER_DESC rd{};
  rd.FillMode = D3D11_FILL_SOLID;
  rd.CullMode = D3D11_CULL_NONE;
  rd.ScissorEnable = FALSE;
  rd.DepthClipEnable = TRUE;
  g_device->CreateRasterizerState(&rd, &g_blit_raster);
  D3D11_BUFFER_DESC constants{};
  constants.ByteWidth = 8 * sizeof(float);
  constants.Usage = D3D11_USAGE_DEFAULT;
  constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  g_device->CreateBuffer(&constants, nullptr, &g_blit_constants);
  D3D11_BUFFER_DESC aa_constants{};
    aa_constants.ByteWidth = 4 * sizeof(float);
    aa_constants.Usage = D3D11_USAGE_DEFAULT;
    aa_constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    g_device->CreateBuffer(&aa_constants, nullptr, &g_source_projection_constants);
    return g_blit_vs && g_blit_ps && g_blit_sampler && g_blit_blend &&
         g_blit_depth && g_blit_raster && g_blit_constants &&
         g_source_projection_constants;
}

bool EnsurePostProcessing() {
  if (g_post_fxaa_ps && g_post_cas_ps &&
      g_post_constants && g_cas_constants)
    return true;
  const auto release = [](auto*& resource) {
    if (resource) resource->Release();
    resource = nullptr;
  };
  release(g_post_fxaa_ps);
  release(g_post_cas_ps);
  release(g_post_constants);
  release(g_cas_constants);
  // The CAS pass below adapts AMD GPUOpen's no-scaling FidelityFX CAS kernel;
  // the required MIT notice is kept in fidelityfx_cas_license.txt.
  static const char* shader =
      "Texture2D tex : register(t0); SamplerState samp : register(s0);"
      "cbuffer Post : register(b0) { float4 size; };"
      "cbuffer Sharp : register(b1) { float4 sharp; };"
      "struct PSIn { float4 pos : SV_Position; float2 uv : TEXCOORD0; };"
      "float Luma(float3 c) { return dot(c,float3(0.299,0.587,0.114)); }"
      "float4 PSFxaa(PSIn i) : SV_Target {"
      "float3 rgbM=tex.Sample(samp,i.uv).rgb; float lM=Luma(rgbM);"
      "float lN=Luma(tex.Sample(samp,i.uv+float2(0,-size.y)).rgb);"
      "float lS=Luma(tex.Sample(samp,i.uv+float2(0,size.y)).rgb);"
      "float lW=Luma(tex.Sample(samp,i.uv+float2(-size.x,0)).rgb);"
      "float lE=Luma(tex.Sample(samp,i.uv+float2(size.x,0)).rgb);"
      "float lMin=min(lM,min(min(lN,lS),min(lW,lE)));"
      "float lMax=max(lM,max(max(lN,lS),max(lW,lE)));"
      "float range=lMax-lMin; if(range<max(0.0312,lMax*0.125))"
      "return float4(rgbM,1);"
      "float2 dir=float2(-((lN-lS)),lW-lE);"
      "float reduce=max((lN+lS+lW+lE)*0.03125,0.0078125);"
      "float rcp=min(abs(dir.x),abs(dir.y));"
      "dir=clamp(dir/(rcp+reduce),-8.0,8.0)*size.xy;"
      "float3 a=0.5*(tex.Sample(samp,i.uv+dir*(1.0/3.0-0.5)).rgb+"
      "tex.Sample(samp,i.uv+dir*(2.0/3.0-0.5)).rgb);"
      "float3 b=a*0.5+0.25*(tex.Sample(samp,i.uv+dir*(-0.5)).rgb+"
      "tex.Sample(samp,i.uv+dir*0.5).rgb);"
      "float lB=Luma(b); float3 result=(lB<lMin || lB>lMax)?a:b;"
      "return float4(result,1); }"
      "float3 LoadRgb(int2 p) {"
      "p=clamp(p,int2(0,0),int2(size.zw)-1);"
      "return tex.Load(int3(p,0)).rgb; }"
      "float4 PSCas(PSIn i) : SV_Target {"
      "int2 p=clamp(int2(i.pos.xy),int2(0,0),int2(size.zw)-1);"
      "float3 a=LoadRgb(p+int2(-1,-1)); float3 b=LoadRgb(p+int2(0,-1));"
      "float3 c=LoadRgb(p+int2(1,-1)); float3 d=LoadRgb(p+int2(-1,0));"
      "float3 e=LoadRgb(p); float3 f=LoadRgb(p+int2(1,0));"
      "float3 g=LoadRgb(p+int2(-1,1)); float3 h=LoadRgb(p+int2(0,1));"
      "float3 j=LoadRgb(p+int2(1,1));"
      "float3 mn=min(min(d,e),min(f,min(b,h)));"
      "float3 mx=max(max(d,e),max(f,max(b,h)));"
      "float3 amp=saturate(min(mn,1.0-mx)/max(mx,0.0001));"
      "amp=sqrt(amp); float peak=-rcp(lerp(8.0,5.0,saturate(sharp.x)));"
      "float3 w=amp*peak;"
      "float3 color=saturate((b*w+d*w+f*w+h*w+e)/(1.0+4.0*w));"
      "return float4(color,1); }";
  ID3DBlob* fxaa_blob = nullptr;
  ID3DBlob* cas_blob = nullptr;
  ID3DBlob* error = nullptr;
  const auto compile = [&](const char* entry, ID3DBlob** blob) {
    if (error) { error->Release(); error = nullptr; }
    const HRESULT result = D3DCompile(shader, std::strlen(shader), nullptr,
        nullptr, nullptr, entry, "ps_4_0", 0, 0, blob, &error);
    if (FAILED(result) && error)
      DSVR_ERROR("XR: %s post shader compile failed: %.*s", entry,
          static_cast<int>(error->GetBufferSize()),
          static_cast<const char*>(error->GetBufferPointer()));
    return SUCCEEDED(result);
  };
  if (!compile("PSFxaa", &fxaa_blob) || !compile("PSCas", &cas_blob)) {
    release(fxaa_blob); release(cas_blob); release(error);
    return false;
  }
  HRESULT result = g_device->CreatePixelShader(fxaa_blob->GetBufferPointer(),
      fxaa_blob->GetBufferSize(), nullptr, &g_post_fxaa_ps);
  if (SUCCEEDED(result))
    result = g_device->CreatePixelShader(cas_blob->GetBufferPointer(),
        cas_blob->GetBufferSize(), nullptr, &g_post_cas_ps);
  release(fxaa_blob); release(cas_blob); release(error);
  D3D11_BUFFER_DESC buffer{};
  buffer.ByteWidth = 4 * sizeof(float);
  buffer.Usage = D3D11_USAGE_DEFAULT;
  buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  if (FAILED(result) ||
      FAILED(g_device->CreateBuffer(&buffer, nullptr, &g_post_constants)) ||
      FAILED(g_device->CreateBuffer(&buffer, nullptr, &g_cas_constants))) {
    release(g_post_fxaa_ps); release(g_post_cas_ps);
    release(g_post_constants); release(g_cas_constants);
    return false;
  }
  return true;
}

// Theater floor grid: one small alpha swapchain rendered with a procedural
// line pass, submitted as a blended quad lying on the judged STAGE floor.
// Entirely optional: any failure disables it for the session without
// touching the main render path.
bool EnsureGridPipeline() {
  if (g_grid_vs && g_grid_ps && g_grid_constants && g_grid_blend) return true;
  const auto release = [](auto*& p) { if (p) p->Release(); p = nullptr; };
  release(g_grid_vs); release(g_grid_ps); release(g_grid_constants);
  release(g_grid_blend);
  static const char* kGridShader =
    "cbuffer Grid : register(b0) { float4 gc; };"
    "struct PSIn { float4 pos : SV_Position; float2 uv : TEXCOORD0; };"
    "PSIn VSMain(uint id : SV_VertexID) { PSIn o;"
    "  o.uv = float2((id << 1) & 2, id & 2);"
    "  o.pos = float4(o.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);"
    "  return o; }"
    "float Lines(float2 m, float cell, float w) {"
    "  float2 f = frac(m / cell);"
    "  float2 d = min(f, 1.0 - f) * cell;"
    "  return max(1.0 - smoothstep(0.0, w, d.x),"
    "             1.0 - smoothstep(0.0, w, d.y)); }"
    "float4 PSGrid(PSIn i) : SV_Target {"
      "  float2 m = (i.uv - 0.5) * gc.xy;"
    "  float a = Lines(m, 0.5, 0.012) * gc.z;"
      "  float edge = min(min(i.uv.x, 1.0-i.uv.x),"
      "                   min(i.uv.y, 1.0-i.uv.y));"
      "  a = max(a, (1.0-smoothstep(0.0, 0.008, edge))*0.55);"
    "  return float4(0.65, 0.72, 0.85, saturate(a)); }";
  ID3DBlob* vs_blob = nullptr;
  ID3DBlob* ps_blob = nullptr;
  ID3DBlob* err = nullptr;
  if (FAILED(D3DCompile(kGridShader, strlen(kGridShader), nullptr, nullptr,
                        nullptr, "VSMain", "vs_4_0", 0, 0, &vs_blob, &err)) ||
      FAILED(D3DCompile(kGridShader, strlen(kGridShader), nullptr, nullptr,
                        nullptr, "PSGrid", "ps_4_0", 0, 0, &ps_blob, &err))) {
    DSVR_ERROR("XR: floor grid shader compile failed");
    release(vs_blob); release(ps_blob); release(err);
    g_grid_failed = true;
    return false;
  }
  g_device->CreateVertexShader(vs_blob->GetBufferPointer(),
                               vs_blob->GetBufferSize(), nullptr, &g_grid_vs);
  g_device->CreatePixelShader(ps_blob->GetBufferPointer(),
                              ps_blob->GetBufferSize(), nullptr, &g_grid_ps);
  vs_blob->Release();
  ps_blob->Release();
  release(err);
  D3D11_BUFFER_DESC cd{};
  cd.ByteWidth = 4 * sizeof(float);
  cd.Usage = D3D11_USAGE_DEFAULT;
  cd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  g_device->CreateBuffer(&cd, nullptr, &g_grid_constants);
  D3D11_BLEND_DESC bd{};
  bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  g_device->CreateBlendState(&bd, &g_grid_blend);
  if (!g_grid_vs || !g_grid_ps || !g_grid_constants || !g_grid_blend) {
    release(g_grid_vs); release(g_grid_ps); release(g_grid_constants);
    release(g_grid_blend);
    g_grid_failed = true;
    return false;
  }
  return true;
}

bool EnsureCalibrationRoomPipeline() {
  if (g_calibration_room_ps && g_calibration_room_constants) return true;
  if (g_calibration_room_ps) {
    g_calibration_room_ps->Release();
    g_calibration_room_ps = nullptr;
  }
  if (g_calibration_room_constants) {
    g_calibration_room_constants->Release();
    g_calibration_room_constants = nullptr;
  }
  static const char* shader =
      "Texture2D panel : register(t0); SamplerState samp : register(s0);"
      "cbuffer Room : register(b0) { float4 tangents; float4 eye_pos; "
      "float4 eye_rot; float4 stage_pos; float4 stage_rot; float4 room; "
      "float4 panel_pos; float4 panel_size; };"
      "struct PSIn { float4 pos : SV_Position; float2 uv : TEXCOORD0; };"
      "float3 Rotate(float3 v,float4 q) { return v+2.0*cross(q.xyz,"
      "cross(q.xyz,v)+q.w*v); }"
      "float3 FogTone(float3 dir) {"
      "float h=saturate(dir.y*0.55+0.48);"
      "float w=0.5+0.5*sin(dir.x*19.0+sin(dir.z*17.0+dir.y*11.0)*1.7);"
      "float mist=smoothstep(0.12,0.92,w);"
      "return lerp(float3(0.008,0.017,0.012),float3(0.018,0.039,0.025),h)+"
      "float3(0.004,0.012,0.006)*mist; }"
      "float3 RoomColor(float3 origin,float3 dir) {"
      "float3 haze=FogTone(dir); float3 c=haze;"
      "float hx=room.x,hz=room.y,wallH=room.z; float wallT=1e20;"
      "float3 wallP=0;"
      "if(abs(dir.x)>1e-5) { float wx=dir.x>0?hx:-hx;"
      "float tx=(wx-origin.x)/dir.x; float3 px=origin+dir*tx;"
      "if(tx>0 && px.y>=0 && px.y<=wallH && abs(px.z)<=hz) {"
      "wallT=tx; wallP=px; } }"
      "if(abs(dir.z)>1e-5) { float wz=dir.z>0?hz:-hz;"
      "float tz=(wz-origin.z)/dir.z; float3 pz=origin+dir*tz;"
      "if(tz>0 && tz<wallT && pz.y>=0 && pz.y<=wallH && abs(pz.x)<=hx) {"
      "wallT=tz; wallP=pz; } }"
      "if(wallT<1e19) { float y=wallP.y/wallH;"
      "float3 wall=lerp(float3(0.010,0.021,0.014),"
      "float3(0.023,0.046,0.029),y);"
      "float edge=1.0-smoothstep(0.015,0.035,min(hx-abs(wallP.x),hz-abs(wallP.z)));"
      "wall+=float3(0.025,0.052,0.029)*edge;"
      "float fog=saturate(1.0-exp(-wallT*0.36));"
      "c=lerp(wall,haze,fog); }"
      "if(room.w>0.5 && dir.y < -1e-5) { float tf=-origin.y/dir.y;"
      "float3 p=origin+dir*tf;"
      "if(tf>0 && tf<wallT) {"
      "float2 q=p.xz; float2 cell=abs(frac(q*2.0+0.5)-0.5)*0.5;"
      "float aa=max(fwidth(q.x),fwidth(q.y))*0.85+0.003;"
      "float minor=1.0-smoothstep(0.0,aa,min(cell.x,cell.y));"
      "float2 majorCell=abs(frac(q+0.5)-0.5);"
      "float major=1.0-smoothstep(0.0,aa*1.35,min(majorCell.x,majorCell.y));"
      "float3 floor_color=float3(0.008,0.016,0.011)+"
      "float3(0.021,0.047,0.028)*minor+float3(0.028,0.061,0.034)*major;"
      "float fog=saturate(1.0-exp(-tf*0.085));"
      "c=lerp(floor_color,haze,fog);"
      "} } return c; }"
      "float4 PSMain(PSIn i) : SV_Target {"
      "float2 t=float2(lerp(tangents.x,tangents.y,i.uv.x),"
      "lerp(tangents.z,tangents.w,i.uv.y));"
      "float3 local=normalize(float3(t.x,t.y,-1));"
      "float3 dir=normalize(Rotate(local,eye_rot));"
      "float4 inv_stage=float4(-stage_rot.xyz,stage_rot.w);"
      "float3 origin=Rotate(eye_pos.xyz-stage_pos.xyz,inv_stage);"
      "float3 stage_dir=Rotate(dir,inv_stage);"
      "float3 color=RoomColor(origin,stage_dir);"
      "float3 normal=float3(sin(panel_pos.w),0,cos(panel_pos.w));"
      "float3 right=float3(cos(panel_pos.w),0,-sin(panel_pos.w));"
      "float den=dot(dir,normal);"
      "if(den < -1e-5) { float ui_t=dot(panel_pos.xyz-eye_pos.xyz,normal)/den;"
      "if(ui_t>0) { float3 hit=eye_pos.xyz+dir*ui_t;"
      "float3 rel=hit-panel_pos.xyz; float2 uv=float2(0.5+dot(rel,right)/panel_size.x,"
      "0.5-rel.y/panel_size.y);"
      "if(all(uv>=0) && all(uv<=1)) {"
      "float panel_aspect=panel_size.x/panel_size.y;"
      "float source_aspect=panel_size.w; float2 source_uv=uv;"
      "if(source_aspect>panel_aspect) { float visible=panel_aspect/source_aspect;"
      "float top=(1.0-visible)*0.5; if(uv.y<top || uv.y>top+visible)"
      "return float4(color,1); source_uv.y=(uv.y-top)/visible; }"
      "else if(source_aspect<panel_aspect) { float visible=source_aspect/panel_aspect;"
      "float left=(1.0-visible)*0.5; if(uv.x<left || uv.x>left+visible)"
      "return float4(color,1); source_uv.x=(uv.x-left)/visible; }"
      "if(panel_size.z>0.5) { float4 ink=panel.Sample(samp,source_uv);"
      "return float4(eye_pos.w>0.5?ink.rgb+color*(1.0-saturate(ink.a)):ink.rgb,1); }"
      "float border=min(min(uv.x,1.0-uv.x),min(uv.y,1.0-uv.y));"
      "float glow=1.0-smoothstep(0.0,0.012,border);"
      "float4 ink=float4(float3(0.018,0.038,0.024)+"
      "float3(0.08,0.14,0.06)*glow,0.42);"
      "return float4(lerp(color,ink.rgb,ink.a),1); } } }"
      "return float4(color,1); }";
  ID3DBlob* ps_blob = nullptr;
  ID3DBlob* err = nullptr;
  if (FAILED(D3DCompile(shader, strlen(shader), nullptr, nullptr, nullptr,
                        "PSMain", "ps_4_0", 0, 0, &ps_blob, &err))) {
    if (err) {
      DSVR_ERROR("XR: calibration room shader compile failed: %.*s",
                 static_cast<int>(err->GetBufferSize()),
                 static_cast<const char*>(err->GetBufferPointer()));
      err->Release();
    }
    if (ps_blob) ps_blob->Release();
    return false;
  }
  if (err) err->Release();
  const HRESULT shader_result = g_device->CreatePixelShader(
      ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr,
      &g_calibration_room_ps);
  ps_blob->Release();
  D3D11_BUFFER_DESC desc{};
  desc.ByteWidth = 8 * sizeof(float) * 4;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  if (FAILED(shader_result) || !g_calibration_room_ps ||
      FAILED(g_device->CreateBuffer(&desc, nullptr,
                                    &g_calibration_room_constants))) {
    if (g_calibration_room_ps) g_calibration_room_ps->Release();
    g_calibration_room_ps = nullptr;
    if (g_calibration_room_constants) g_calibration_room_constants->Release();
    g_calibration_room_constants = nullptr;
    return false;
  }
  return true;
}

bool DrawImmersiveRoom(int eye, uint32_t index, const XrView& view,
                       const D3D11_TEXTURE2D_DESC& panel_desc,
                       ID3D11ShaderResourceView* panel_srv,
                       const MenuAnchor& anchor, float panel_width,
                       bool floor_grid, bool transparent_panel) {
  if (index >= g_eyes[eye].rtvs.size() ||
      !g_eyes[eye].rtvs[index] || !EnsureCalibrationRoomPipeline())
    return false;
  auto* rtv = g_eyes[eye].stage_rtv ? g_eyes[eye].stage_rtv : g_eyes[eye].rtvs[index];
  const float fallback[4]{0.006f, 0.012f, 0.009f, 1.0f};
  g_context->ClearRenderTargetView(rtv, fallback);
  g_context->OMSetRenderTargets(1, &rtv, nullptr);
  g_context->OMSetBlendState(g_blit_blend, nullptr, 0xFFFFFFFF);
  g_context->OMSetDepthStencilState(g_blit_depth, 0);
  g_context->RSSetState(g_blit_raster);
  const D3D11_VIEWPORT vp{0.0f, 0.0f, (float)g_eyes[eye].width,
                          (float)g_eyes[eye].height, 0.0f, 1.0f};
  g_context->RSSetViewports(1, &vp);
  g_context->VSSetShader(g_blit_vs, nullptr, 0);
  g_context->PSSetShader(g_calibration_room_ps, nullptr, 0);
  g_context->GSSetShader(nullptr, nullptr, 0);
  g_context->HSSetShader(nullptr, nullptr, 0);
  g_context->DSSetShader(nullptr, nullptr, 0);
  g_context->CSSetShader(nullptr, nullptr, 0);
  g_context->PSSetShaderResources(0, 1, &panel_srv);
  g_context->PSSetSamplers(0, 1, &g_blit_sampler);
  const float source_aspect = panel_desc.Height
      ? (float)panel_desc.Width / (float)panel_desc.Height : kTheaterAspect;
  if (!(panel_width > 0.0f)) panel_width = g_calibration_panel_width;
  const float panel_height = panel_width / kTheaterAspect;
  const float constants[32]{
      std::tan(view.fov.angleLeft), std::tan(view.fov.angleRight),
      std::tan(view.fov.angleUp), std::tan(view.fov.angleDown),
      view.pose.position.x, view.pose.position.y, view.pose.position.z,
      transparent_panel ? 1.0f : 0.0f,
      view.pose.orientation.x, view.pose.orientation.y,
      view.pose.orientation.z, view.pose.orientation.w,
      g_calibration_stage_pose.position.x, g_calibration_stage_pose.position.y,
      g_calibration_stage_pose.position.z, 0,
      g_calibration_stage_pose.orientation.x,
      g_calibration_stage_pose.orientation.y,
      g_calibration_stage_pose.orientation.z,
      g_calibration_stage_pose.orientation.w,
      g_calibration_room_half_x, g_calibration_room_half_z, 2.8f,
      floor_grid ? 1.0f : 0.0f,
      anchor.x, anchor.y, anchor.z,
      anchor.yaw, panel_width, panel_height,
      panel_srv ? 1.0f : 0.0f, source_aspect};
  g_context->UpdateSubresource(g_calibration_room_constants, 0, nullptr,
                               constants, 0, 0);
  ID3D11Buffer* cb = g_calibration_room_constants;
  g_context->PSSetConstantBuffers(0, 1, &cb);
  g_context->IASetInputLayout(nullptr);
  g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  g_context->Draw(3, 0);
  ID3D11ShaderResourceView* none = nullptr;
  g_context->PSSetShaderResources(0, 1, &none);
  g_context->OMSetRenderTargets(0, nullptr, nullptr);
  return true;
}

bool EnsureGridSwapchain() {
  if (g_grid_swapchain != XR_NULL_HANDLE) return true;
  if (g_grid_failed || !g_device || g_eye_format == 0) return false;
  if (!EnsureGridPipeline()) return false;
  constexpr uint32_t kGridSize = 512;
  XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
  info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                    XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
  info.format = g_eye_format;
  info.sampleCount = 1;
  info.width = kGridSize;
  info.height = kGridSize;
  info.faceCount = 1;
  info.arraySize = 1;
  info.mipCount = 1;
  if (!Ok(xrCreateSwapchain(g_session, &info, &g_grid_swapchain),
          "xrCreateSwapchain(grid)")) {
    g_grid_swapchain = XR_NULL_HANDLE;
    g_grid_failed = true;
    return false;
  }
  uint32_t count = 0;
  if (!Ok(xrEnumerateSwapchainImages(g_grid_swapchain, 0, &count, nullptr),
          "xrEnumerateSwapchainImages(grid count)") || !count) {
    g_grid_failed = true;
    return false;
  }
  g_grid_images.assign(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
  const uint32_t capacity = count;
  if (!Ok(xrEnumerateSwapchainImages(g_grid_swapchain, capacity, &count,
         (XrSwapchainImageBaseHeader*)g_grid_images.data()),
         "xrEnumerateSwapchainImages(grid)") || !count || count > capacity) {
    g_grid_failed = true;
    return false;
  }
  g_grid_images.resize(count);
  g_grid_rtvs.assign(count, nullptr);
  for (uint32_t i = 0; i < count; ++i) {
    if (!g_grid_images[i].texture) {
      g_grid_failed = true;
      return false;
    }
    D3D11_RENDER_TARGET_VIEW_DESC rtv_desc{};
    rtv_desc.Format = (DXGI_FORMAT)g_eye_format;
    rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtv_desc.Texture2D.MipSlice = 0;
    if (FAILED(g_device->CreateRenderTargetView(g_grid_images[i].texture,
                                                &rtv_desc, &g_grid_rtvs[i])) ||
        !g_grid_rtvs[i]) {
      g_grid_failed = true;
      return false;
    }
  }
  g_grid_size = kGridSize;
  DSVR_LOG("XR: floor grid swapchain ready %ux%u images=%u", kGridSize,
           kGridSize, (unsigned)g_grid_images.size());
  return true;
}

// Renders and fills grid_quad (layer pose included) when the judged floor
// plane is tracked. Returns false silently on any gate or failure.
bool DrawFloorGrid(XrTime display_time, XrCompositionLayerQuad& grid) {
  if (!DiagnosticsCurrentConfig().theater_floor_grid || !g_floor_space)
    return false;
  if (g_grid_rtvs.empty() && !EnsureGridSwapchain()) return false;
  XrSpaceLocation floor_loc{XR_TYPE_SPACE_LOCATION};
  if (XR_FAILED(xrLocateSpace(g_floor_space, g_space, display_time,
                              &floor_loc)) ||
      !(floor_loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) ||
      !std::isfinite(floor_loc.pose.position.y))
    return false;
  float width = 8.0f, height = 8.0f;
  XrExtent2Df bounds{};
  if (XR_SUCCEEDED(xrGetReferenceSpaceBoundsRect(
          g_session, XR_REFERENCE_SPACE_TYPE_STAGE, &bounds)) &&
      std::isfinite(bounds.width) && std::isfinite(bounds.height) &&
      bounds.width >= 0.5f && bounds.height >= 0.5f &&
      bounds.width <= 1000.0f && bounds.height <= 1000.0f) {
    width = bounds.width;
    height = bounds.height;
  }
  uint32_t index = 0;
  XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
  if (XR_FAILED(xrAcquireSwapchainImage(g_grid_swapchain, &acquire, &index)))
    return false;
  XrSwapchainImageWaitInfo wait_image{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
  wait_image.timeout = 5'000'000;
  const auto waited = xrWaitSwapchainImage(g_grid_swapchain, &wait_image);
  XrSwapchainImageReleaseInfo release_info{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
  if (XR_FAILED(waited) || index >= g_grid_rtvs.size() || !g_grid_rtvs[index]) {
    Ok(xrReleaseSwapchainImage(g_grid_swapchain, &release_info),
       "xrReleaseSwapchainImage(grid)");
    return false;
  }
  ID3D11RenderTargetView* rtv = g_grid_rtvs[index];
  const float clear[4]{0.0f, 0.0f, 0.0f, 0.0f};
  g_context->ClearRenderTargetView(rtv, clear);
  g_context->OMSetRenderTargets(1, &rtv, nullptr);
  g_context->OMSetBlendState(g_grid_blend, nullptr, 0xFFFFFFFF);
  g_context->OMSetDepthStencilState(g_blit_depth, 0);
  g_context->RSSetState(g_blit_raster);
  const D3D11_VIEWPORT vp{0.0f, 0.0f, (float)g_grid_size, (float)g_grid_size,
                          0.0f, 1.0f};
  g_context->RSSetViewports(1, &vp);
  g_context->VSSetShader(g_grid_vs, nullptr, 0);
  g_context->PSSetShader(g_grid_ps, nullptr, 0);
  g_context->GSSetShader(nullptr, nullptr, 0);
  g_context->HSSetShader(nullptr, nullptr, 0);
  g_context->DSSetShader(nullptr, nullptr, 0);
  g_context->CSSetShader(nullptr, nullptr, 0);
  ID3D11ShaderResourceView* none = nullptr;
  g_context->PSSetShaderResources(0, 1, &none);
  const float constants[4]{width, height, 0.30f, 0.0f};
  g_context->UpdateSubresource(g_grid_constants, 0, nullptr, constants, 0, 0);
  g_context->PSSetConstantBuffers(0, 1, &g_grid_constants);
  g_context->IASetInputLayout(nullptr);
  g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  g_context->Draw(3, 0);
  g_context->OMSetRenderTargets(0, nullptr, nullptr);
  if (XR_FAILED(xrReleaseSwapchainImage(g_grid_swapchain, &release_info)))
    return false;
  // Flat on the whole judged floor, a hair above it to avoid z-fighting.
  grid.space = g_space;
  grid.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
  grid.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
  grid.subImage.swapchain = g_grid_swapchain;
  grid.subImage.imageRect.offset = {0, 0};
  grid.subImage.imageRect.extent = {(int32_t)g_grid_size, (int32_t)g_grid_size};
  grid.subImage.imageArrayIndex = 0;
  const auto floor_q = floor_loc.pose.orientation;
  const XrQuaternionf lie_flat{-0.70710678f, 0.0f, 0.0f, 0.70710678f};
  grid.pose.orientation = {
      floor_q.w * lie_flat.x + floor_q.x * lie_flat.w +
          floor_q.y * lie_flat.z - floor_q.z * lie_flat.y,
      floor_q.w * lie_flat.y - floor_q.x * lie_flat.z +
          floor_q.y * lie_flat.w + floor_q.z * lie_flat.x,
      floor_q.w * lie_flat.z + floor_q.x * lie_flat.y -
          floor_q.y * lie_flat.x + floor_q.z * lie_flat.w,
      floor_q.w * lie_flat.w - floor_q.x * lie_flat.x -
          floor_q.y * lie_flat.y - floor_q.z * lie_flat.z};
  const Vec3 up = QuatRotate({0, 1, 0}, floor_q.x, floor_q.y,
                             floor_q.z, floor_q.w);
  grid.pose.position = {
      floor_loc.pose.position.x + up.x * 0.002f,
      floor_loc.pose.position.y + up.y * 0.002f,
      floor_loc.pose.position.z + up.z * 0.002f};
  grid.size.width = width;
  grid.size.height = height;
  return true;
}

bool PrepareMonoSource(ID3D11Texture2D* source,
                       const D3D11_TEXTURE2D_DESC& src_desc) {
  if (!source || !g_device || !g_context || !src_desc.Width || !src_desc.Height ||
      src_desc.ArraySize != 1 || !src_desc.SampleDesc.Count) return false;
  const auto format = SourceColorFormat(src_desc.Format);
  UINT support=0;
  if (format==DXGI_FORMAT_UNKNOWN || FAILED(g_device->CheckFormatSupport(format,&support)) ||
      !(support&D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) ||
      (src_desc.SampleDesc.Count>1 && !(support&D3D11_FORMAT_SUPPORT_MULTISAMPLE_RESOLVE))) return false;
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  source->GetDevice(&device);
  if (device.Get()!=g_device) return false;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> next;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> next_srv;
  const bool replace=!g_stage || !g_blit_srv || g_stage_w!=src_desc.Width ||
      g_stage_h!=src_desc.Height || g_stage_fmt!=format;
  if (replace) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width=src_desc.Width;desc.Height=src_desc.Height;
    desc.MipLevels=desc.ArraySize=1;desc.Format=format;desc.SampleDesc.Count=1;
    desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(g_device->CreateTexture2D(&desc,nullptr,&next)) ||
        FAILED(g_device->CreateShaderResourceView(next.Get(),nullptr,&next_srv))) return false;
  }
  auto* destination=replace ? next.Get() : g_stage;
  if (source!=destination) {
    g_context->OMSetRenderTargets(0,nullptr,nullptr);
    ID3D11ShaderResourceView* none=nullptr;
    g_context->PSSetShaderResources(0,1,&none);
    if (src_desc.SampleDesc.Count>1)
      g_context->ResolveSubresource(destination,0,source,0,format);
    else
      g_context->CopySubresourceRegion(destination,0,0,0,0,source,0,nullptr);
  }
  if (replace) {
    if (g_blit_srv) g_blit_srv->Release();
    if (g_stage) g_stage->Release();
    g_stage=next.Detach();g_blit_srv=next_srv.Detach();
    g_stage_w=src_desc.Width;g_stage_h=src_desc.Height;g_stage_fmt=format;
    DSVR_LOG("XR: completed-source storage %ux%u format=%d",g_stage_w,g_stage_h,(int)format);
  }
  return true;
}

bool EnsureMenuSurface() {
  constexpr uint32_t width = 1920;
  constexpr uint32_t height = 1080;
  if (g_menu_surface && g_menu_surface_rtv && g_menu_surface_srv &&
      g_menu_surface_w == width && g_menu_surface_h == height)
    return true;
  if (g_menu_surface_srv) g_menu_surface_srv->Release();
  if (g_menu_surface_rtv) g_menu_surface_rtv->Release();
  if (g_menu_surface) g_menu_surface->Release();
  g_menu_surface_srv = nullptr;
  g_menu_surface_rtv = nullptr;
  g_menu_surface = nullptr;
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  if (FAILED(g_device->CreateTexture2D(&desc, nullptr, &g_menu_surface)) ||
      FAILED(g_device->CreateRenderTargetView(g_menu_surface, nullptr,
                                               &g_menu_surface_rtv)) ||
      FAILED(g_device->CreateShaderResourceView(g_menu_surface, nullptr,
                                                 &g_menu_surface_srv))) {
    if (g_menu_surface_srv) g_menu_surface_srv->Release();
    if (g_menu_surface_rtv) g_menu_surface_rtv->Release();
    if (g_menu_surface) g_menu_surface->Release();
    g_menu_surface_srv = nullptr;
    g_menu_surface_rtv = nullptr;
    g_menu_surface = nullptr;
    return false;
  }
  g_menu_surface_w = width;
  g_menu_surface_h = height;
  g_menu_surface_fmt = desc.Format;
  DSVR_LOG("XR: independent transparent menu surface ready %ux%u", width,
           height);
  return true;
}

bool EnsureEyeProcessingSurfaces(int eye) {
  const auto format = static_cast<DXGI_FORMAT>(g_eye_format);
  if (g_eyes[eye].stage && g_eyes[eye].stage_rtv && g_eyes[eye].stage_srv &&
      g_eyes[eye].filtered && g_eyes[eye].filtered_rtv && g_eyes[eye].filtered_srv &&
      g_eyes[eye].post_width == g_eyes[eye].width && g_eyes[eye].post_height == g_eyes[eye].height &&
      g_eyes[eye].post_format == format)
    return true;
  const auto release = [](auto*& resource) {
    if (resource) resource->Release();
    resource = nullptr;
  };
  release(g_eyes[eye].stage_srv);
  release(g_eyes[eye].stage_rtv);
  release(g_eyes[eye].stage);
  release(g_eyes[eye].filtered_srv);
  release(g_eyes[eye].filtered_rtv);
  release(g_eyes[eye].filtered);
  g_eyes[eye].post_width = g_eyes[eye].post_height = 0;
  g_eyes[eye].post_format = DXGI_FORMAT_UNKNOWN;
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = g_eyes[eye].width;
  desc.Height = g_eyes[eye].height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = format;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  const auto create = [&](ID3D11Texture2D** texture,
                          ID3D11RenderTargetView** rtv,
                          ID3D11ShaderResourceView** srv) {
    D3D11_RENDER_TARGET_VIEW_DESC rtv_desc{};
    rtv_desc.Format = format;
    rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.Format = format;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;
    return SUCCEEDED(g_device->CreateTexture2D(&desc, nullptr, texture)) &&
        SUCCEEDED(g_device->CreateRenderTargetView(*texture, &rtv_desc, rtv)) &&
        SUCCEEDED(g_device->CreateShaderResourceView(*texture, &srv_desc, srv));
  };
  if (!g_eyes[eye].width || !g_eyes[eye].height ||
      !create(&g_eyes[eye].stage, &g_eyes[eye].stage_rtv, &g_eyes[eye].stage_srv) ||
      !create(&g_eyes[eye].filtered, &g_eyes[eye].filtered_rtv, &g_eyes[eye].filtered_srv)) {
    release(g_eyes[eye].stage_srv);
    release(g_eyes[eye].stage_rtv);
    release(g_eyes[eye].stage);
    release(g_eyes[eye].filtered_srv);
    release(g_eyes[eye].filtered_rtv);
    release(g_eyes[eye].filtered);
    return false;
  }
  g_eyes[eye].post_width = g_eyes[eye].width;
  g_eyes[eye].post_height = g_eyes[eye].height;
  g_eyes[eye].post_format = format;
  DSVR_LOG("XR: full-frame post surfaces ready %ux%u format=%d",
           g_eyes[eye].post_width, g_eyes[eye].post_height, static_cast<int>(format));
  return true;
}

bool BlitToEye(int eye, uint32_t index, const D3D11_TEXTURE2D_DESC& src_desc,
               const XrFovf& fov, float vignette,
               const SymmetricFrustum* source_projection = nullptr) {
  if (!g_blit_srv || index >= g_eyes[eye].rtvs.size()) return false;
  ID3D11RenderTargetView* rtv = g_eyes[eye].stage_rtv
      ? g_eyes[eye].stage_rtv : g_eyes[eye].rtvs[index];
  if (!rtv) {
    DSVR_ERROR("XR: blit skipped, null RTV eye=%d index=%u", eye, index);
    return false;
  }
  // Aspect-preserving fit of the game image into this eye's virtual-screen
  // rect (full eye rect until the first located view sets it).
  float rx = g_screen_rect[eye][2] > 0 ? g_screen_rect[eye][0] : 0.0f;
  float ry = g_screen_rect[eye][2] > 0 ? g_screen_rect[eye][1] : 0.0f;
  float rw = g_screen_rect[eye][2] > 0 ? g_screen_rect[eye][2]
                                       : (float)g_eyes[eye].width;
  float rh = g_screen_rect[eye][3] > 0 ? g_screen_rect[eye][3]
                                       : (float)g_eyes[eye].height;
  D3D11_VIEWPORT vp{};
  if (g_immersive) {
    vp = D3D11_VIEWPORT{0, 0, static_cast<float>(g_eyes[eye].width),
        static_cast<float>(g_eyes[eye].height), 0.0f, 1.0f};
  } else {
    float scale = std::min(rw / (float)src_desc.Width,
                           rh / (float)src_desc.Height);
    float w = src_desc.Width * scale, h = src_desc.Height * scale;
    vp = D3D11_VIEWPORT{rx + (rw - w) * 0.5f, ry + (rh - h) * 0.5f, w, h,
                        0.0f, 1.0f};
  }
  // The fullscreen blit overwrites every pixel when the viewport exactly
  // covers the eye. Skip the redundant clear only in that case; keep it for
  // theater letterboxing and asymmetric-FOV subrects where black surround
  // is intentional.
  const bool full_eye = g_immersive || (rx == 0.0f && ry == 0.0f &&
      rw >= (float)g_eyes[eye].width && rh >= (float)g_eyes[eye].height);
  if (!full_eye) {
    const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    g_context->ClearRenderTargetView(rtv, black);
  }
  g_context->OMSetRenderTargets(1, &rtv, nullptr);
  g_context->OMSetBlendState(g_blit_blend, nullptr, 0xFFFFFFFF);
  g_context->OMSetDepthStencilState(g_blit_depth, 0);
  g_context->RSSetState(g_blit_raster);
  g_context->RSSetViewports(1, &vp);
  g_context->VSSetShader(g_blit_vs, nullptr, 0);
  g_context->PSSetShader(g_blit_ps, nullptr, 0);
  g_context->GSSetShader(nullptr, nullptr, 0);
  g_context->HSSetShader(nullptr, nullptr, 0);
  g_context->DSSetShader(nullptr, nullptr, 0);
  g_context->CSSetShader(nullptr, nullptr, 0);
  g_context->PSSetShaderResources(0, 1, &g_blit_srv);
  g_context->PSSetSamplers(0, 1, &g_blit_sampler);
  const float constants[8]{std::tan(fov.angleLeft), std::tan(fov.angleRight),
      std::tan(fov.angleUp), std::tan(fov.angleDown), vignette,
      1.f / g_eyes[eye].width, 1.f / g_eyes[eye].height, 0};
  g_context->UpdateSubresource(g_blit_constants, 0, nullptr, constants, 0, 0);
  g_context->PSSetConstantBuffers(0, 1, &g_blit_constants);
  // Spatial AA and sharpening run after the complete eye image is composed.
  {
    const float aa_c[4]{source_projection ? source_projection->tan_half_h : 1.0f,
        source_projection ? source_projection->tan_half_v : 1.0f,
        source_projection ? 1.0f : 0.0f, 0};
    g_context->UpdateSubresource(g_source_projection_constants, 0, nullptr, aa_c, 0, 0);
    g_context->PSSetConstantBuffers(1, 1, &g_source_projection_constants);
  }
  g_context->IASetInputLayout(nullptr);
  g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  g_context->Draw(3, 0);
  ID3D11ShaderResourceView* none = nullptr;
  g_context->PSSetShaderResources(0, 1, &none);
  g_context->OMSetRenderTargets(0, nullptr, nullptr);
  return true;
}

bool DrawPostPass(int eye, ID3D11PixelShader* shader, ID3D11RenderTargetView* target,
                  ID3D11ShaderResourceView* source,
                  const float size_constants[4],
                  const float cas_constants[4]) {
  if (!shader || !target || !source || !EnsureBlit()) return false;
  D3D11_VIEWPORT viewport{0, 0, static_cast<float>(g_eyes[eye].width),
                          static_cast<float>(g_eyes[eye].height), 0, 1};
  g_context->OMSetRenderTargets(1, &target, nullptr);
  g_context->OMSetBlendState(g_blit_blend, nullptr, 0xFFFFFFFF);
  g_context->OMSetDepthStencilState(g_blit_depth, 0);
  g_context->RSSetState(g_blit_raster);
  g_context->RSSetViewports(1, &viewport);
  g_context->VSSetShader(g_blit_vs, nullptr, 0);
  g_context->PSSetShader(shader, nullptr, 0);
  g_context->GSSetShader(nullptr, nullptr, 0);
  g_context->HSSetShader(nullptr, nullptr, 0);
  g_context->DSSetShader(nullptr, nullptr, 0);
  g_context->CSSetShader(nullptr, nullptr, 0);
  g_context->PSSetShaderResources(0, 1, &source);
  g_context->PSSetSamplers(0, 1, &g_blit_sampler);
  if (size_constants && g_post_constants) {
    g_context->UpdateSubresource(g_post_constants, 0, nullptr,
                                size_constants, 0, 0);
    ID3D11Buffer* constants = g_post_constants;
    g_context->PSSetConstantBuffers(0, 1, &constants);
  }
  if (cas_constants && g_cas_constants) {
    g_context->UpdateSubresource(g_cas_constants, 0, nullptr,
                                cas_constants, 0, 0);
    ID3D11Buffer* constants = g_cas_constants;
    g_context->PSSetConstantBuffers(1, 1, &constants);
  }
  g_context->IASetInputLayout(nullptr);
  g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  g_context->Draw(3, 0);
  ID3D11ShaderResourceView* none = nullptr;
  g_context->PSSetShaderResources(0, 1, &none);
  g_context->OMSetRenderTargets(0, nullptr, nullptr);
  return true;
}

bool ApplyFullImageEffects(int eye, uint32_t index, const Config& config) {
  if (index >= g_eyes[eye].rtvs.size() || index >= g_eyes[eye].images.size() ||
      !g_eyes[eye].rtvs[index] || !g_eyes[eye].images[index].texture ||
      !g_eyes[eye].stage || !g_eyes[eye].stage_srv || !EnsurePostProcessing())
    return false;
  const float size[4]{
      g_eyes[eye].width ? 1.0f / static_cast<float>(g_eyes[eye].width) : 0.0f,
      g_eyes[eye].height ? 1.0f / static_cast<float>(g_eyes[eye].height) : 0.0f,
      static_cast<float>(g_eyes[eye].width), static_cast<float>(g_eyes[eye].height)};
  ID3D11ShaderResourceView* current = g_eyes[eye].stage_srv;
  if (config.aa_mode == 1) {
    if (!DrawPostPass(eye, g_post_fxaa_ps, g_eyes[eye].filtered_rtv, current, size,
                      nullptr))
      return false;
    current = g_eyes[eye].filtered_srv;
  }
  if (config.cas_enabled && config.cas_strength > 0.001f) {
    const float sharp[4]{config.cas_strength, 0, 0, 0};
    return DrawPostPass(eye, g_post_cas_ps, g_eyes[eye].rtvs[index], current, size,
                        sharp);
  }
  ID3D11ShaderResourceView* none = nullptr;
  g_context->PSSetShaderResources(0, 1, &none);
  g_context->OMSetRenderTargets(0, nullptr, nullptr);
  g_context->CopyResource(g_eyes[eye].images[index].texture,
                          config.aa_mode == 1 ? g_eyes[eye].filtered : g_eyes[eye].stage);
  return true;
}


bool EnsureSwapchains(ID3D11Texture2D* backbuffer) {
  D3D11_TEXTURE2D_DESC desc{};
  backbuffer->GetDesc(&desc);
  // One eye size for the whole session: the virtual-screen rect is computed
  // from g_eyes[eye].width/height, so rebuilding the swapchains mid-session leaves
  // that rect pointing at extents that no longer exist. Sizing comes from the
  // runtime's recommended rect, which is what the compositor costs fairly.
  if (g_eyes[0].swapchain && g_eyes[1].swapchain && !g_eyes[0].images.empty() &&
      !g_eyes[1].images.empty() && g_head_space && !g_destroy_pending) return true;
  if (!DestroySwapchains()) return false;
  bool complete = false;
  struct SetupGuard {
    bool& complete;
    ~SetupGuard() { if (!complete) DestroySwapchains(); }
  } setup_guard{complete};
  if (!EnsureBlit()) return false;
  if (!g_eye_scale_percent)
    g_eye_scale_percent = std::clamp(DiagnosticsCurrentConfig().render_scale_percent, 50, 200);

  uint32_t fmt_count = 0;
  if (!Ok(xrEnumerateSwapchainFormats(g_session, 0, &fmt_count, nullptr),
          "xrEnumerateSwapchainFormats(count)") || !fmt_count)
    return false;
  std::vector<int64_t> formats(fmt_count);
  const uint32_t format_capacity = fmt_count;
  if (!Ok(xrEnumerateSwapchainFormats(g_session, format_capacity, &fmt_count,
                                     formats.data()), "xrEnumerateSwapchainFormats") ||
      !fmt_count || fmt_count > format_capacity) return false;
  formats.resize(fmt_count);
  if (!g_format_logged) {
    g_format_logged = true;
    std::string list;
    char tmp[32];
    for (int64_t f : formats) {
      snprintf(tmp, sizeof(tmp), "%lld ", (long long)f);
      list += tmp;
    }
    DSVR_LOG("XR: runtime swapchain formats: %s(game renders %d)", list.c_str(),
             (int)desc.Format);
  }
  // Prefer sRGB RGBA, then linear RGBA, then BGRA variants. The blit shader
  // converts channel order; CopyResource (same-format only) is not used.
  const int64_t kPrefs[] = {29, 28, 91, 87, 30, 32};
  int64_t chosen = formats.empty() ? 28 : formats[0];
  for (int64_t pref : kPrefs) {
    bool found = false;
    for (int64_t f : formats)
      if (f == pref) found = true;
    if (found) {
      chosen = pref;
      break;
    }
  }

  if (!g_space) {
    XrReferenceSpaceCreateInfo ref{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    ref.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    ref.poseInReferenceSpace.orientation.w = 1.0f;
    if (XR_FAILED(xrCreateReferenceSpace(g_session, &ref, &g_space))) {
      g_local_space = false;
      DSVR_WARN("XR: LOCAL space unavailable; theater uses projection fallback");
      ref.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
      if (!Ok(xrCreateReferenceSpace(g_session, &ref, &g_space),
              "xrCreateReferenceSpace(VIEW fallback)"))
        return false;
    }
  }
  if (!g_head_space) {
    XrReferenceSpaceCreateInfo head{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    head.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    head.poseInReferenceSpace.orientation.w = 1.0f;
    if (!Ok(xrCreateReferenceSpace(g_session, &head, &g_head_space),
            "xrCreateReferenceSpace(VIEW head)")) return false;
  }
  if (!g_context) {
    g_device->GetImmediateContext(&g_context);
    if (!g_context) return false;
  }
  if (!g_floor_attempted) {
    g_floor_attempted = true;
    XrReferenceSpaceCreateInfo floor{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    floor.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    floor.poseInReferenceSpace.orientation.w = 1;
    const auto result = xrCreateReferenceSpace(g_session, &floor, &g_floor_space);
    if (XR_FAILED(result)) {
      g_floor_space = XR_NULL_HANDLE;
      DSVR_LOG("XR: floor reference unavailable; calibration preserves setup eye height");
    }
  }

  // The game frame is already resolved at Present. Sampling it into an MSAA
  // eye target does not antialias the scene, so keep the XR swapchains 1x.
  constexpr uint32_t want_samples = 1;
  for (int eye = 0; eye < 2; ++eye) {
      if (!g_eyes[eye].recommended_width || !g_eyes[eye].recommended_height) {
        DSVR_ERROR("XR: eye%d has no runtime recommendation; deferring allocation", eye);
        return false;
      }
    const auto chosen_size = ChooseEyeBufferSize(g_eyes[eye].recommended_width, g_eyes[eye].recommended_height, g_eyes[eye].max_width,
                                               g_eyes[eye].max_height, desc.Width, desc.Height,
                                               g_eye_scale_percent);
    const uint32_t eye_width = chosen_size.width;
    const uint32_t eye_height = chosen_size.height;
    if (!eye_width || !eye_height) {
      DSVR_ERROR("XR: invalid runtime resolution for eye%d", eye);
      return false;
    }
    g_eyes[eye].width = eye_width;
    g_eyes[eye].height = eye_height;
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                        XR_SWAPCHAIN_USAGE_SAMPLED_BIT |
                      XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT |
                      XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT;
    info.format = chosen;
    info.sampleCount = want_samples;
    info.width = eye_width;
    info.height = eye_height;
    info.faceCount = 1;
    info.arraySize = 1;
    info.mipCount = 1;
    if (!Ok(xrCreateSwapchain(g_session, &info, &g_eyes[eye].swapchain),
            "xrCreateSwapchain"))
      return false;
    uint32_t count = 0;
    if (!Ok(xrEnumerateSwapchainImages(g_eyes[eye].swapchain, 0, &count, nullptr),
            "xrEnumerateSwapchainImages(count)") || !count) return false;
    g_eyes[eye].images.assign(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    const uint32_t capacity = count;
    if (!Ok(xrEnumerateSwapchainImages(g_eyes[eye].swapchain, capacity, &count,
               (XrSwapchainImageBaseHeader*)g_eyes[eye].images.data()),
            "xrEnumerateSwapchainImages") || !count || count > capacity) return false;
    g_eyes[eye].images.resize(count);
    g_eyes[eye].rtvs.assign(count, nullptr);
    for (uint32_t i = 0; i < count; ++i) {
      if (!g_eyes[eye].images[i].texture) return false;
      D3D11_TEXTURE2D_DESC actual{};
      g_eyes[eye].images[i].texture->GetDesc(&actual);
      if (actual.Width != eye_width || actual.Height != eye_height ||
          actual.ArraySize != 1 || actual.SampleDesc.Count != 1) {
        DSVR_ERROR("XR: eye%d image%u does not match its requested extent/sample layout", eye, i);
        return false;
      }
      // SteamVR swapchain textures are TYPELESS: a default RTV inherits the
      // typeless format and fails with E_INVALIDARG (found live). An
      // explicit RTV format (the swapchain format) is required.
      D3D11_RENDER_TARGET_VIEW_DESC rtv_desc{};
      rtv_desc.Format = (DXGI_FORMAT)chosen;
      rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
      rtv_desc.Texture2D.MipSlice = 0;
        HRESULT hr = g_device->CreateRenderTargetView(
          g_eyes[eye].images[i].texture, &rtv_desc, &g_eyes[eye].rtvs[i]);
      if (FAILED(hr) || !g_eyes[eye].rtvs[i]) {
        D3D11_TEXTURE2D_DESC td{};
        g_eyes[eye].images[i].texture->GetDesc(&td);
        DSVR_ERROR("XR: eye%d swapchain image%u RTV failed hr=0x%08lx tex=%p "
                   "texformat=%d bind=0x%x",
                   eye, i, (unsigned long)hr,
                   (void*)g_eyes[eye].images[i].texture, (int)td.Format,
                    td.BindFlags);
        return false;
      }
    }
    DSVR_LOG("XR: eye%d RTVs created first=%p", eye, (void*)g_eyes[eye].rtvs[0]);
  }
  g_eye_format = chosen;
  complete = true;
  for (int eye = 0; eye < 2; ++eye)
    DSVR_LOG("XR: eye%d allocated=%ux%u submitted=%ux%u recommended=%ux%u max=%ux%u scale=%d%% format=%d",
        eye, g_eyes[eye].width, g_eyes[eye].height,
        g_eyes[eye].width, g_eyes[eye].height,
        g_eyes[eye].recommended_width, g_eyes[eye].recommended_height,
        g_eyes[eye].max_width, g_eyes[eye].max_height, g_eye_scale_percent, (int)chosen);
  return true;
}

}  // namespace

void XrOnDeviceCaptured(void* d3d11_device) {
  if (!d3d11_device) return;
  if (g_xr_thread && g_xr_thread != GetCurrentThreadId()) return;
  g_xr_thread = GetCurrentThreadId();
  // Retry with throttle: the runtime/HMD may not be ready at first Present
  // (headset asleep, streamer still connecting). Latching one failed
  // attempt forever (v0.4.0 first test: xrCreateInstance -4) bricks VR for
  // the whole session, so failed attempts retry every 3 s until a session
  // exists. Called from XrOnPresentFrame while no session exists.
  unsigned long long now = GetTickCount64();
  if (now < g_next_retry_ms.load()) return;
  g_next_retry_ms.store(now + 3000);
  g_device = (ID3D11Device*)d3d11_device;  // ref held by render module
  DepthProbeInstall(g_device);
  if(g_session)return;
  if(!g_instance) { // retain the instance while retrying later setup stages

  XrApplicationInfo app{};
  strcpy_s(app.applicationName, "DSVR");
  app.applicationVersion = 1;
  strcpy_s(app.engineName, "DSVR");
  app.engineVersion = 1;
  // Request OpenXR 1.0 exactly: required APIs are 1.0 core +
  // XR_KHR_D3D11_enable (physical refresh reporting is optional).
  // XR_CURRENT_API_VERSION (1.1.49) is rejected with
  // XR_ERROR_API_VERSION_UNSUPPORTED (-4) by runtimes capped at 1.0 --
  // seen live when SteamVR theater hosted the launch (2026-10-12), while
  // the same binary worked against VirtualDesktopXR directly. 1.0 keeps
  // DSVR compatible with ANY runtime, per the project constraint.
  app.apiVersion = XR_MAKE_VERSION(1, 0, 0);

  // Check D3D11 extension before creating anything session-bound.
  uint32_t ext_count = 0;
  if (!Ok(xrEnumerateInstanceExtensionProperties(nullptr, 0, &ext_count, nullptr),
          "xrEnumerateInstanceExtensionProperties(count)")) return;
  std::vector<XrExtensionProperties> exts(
      ext_count, {XR_TYPE_EXTENSION_PROPERTIES});
  const uint32_t ext_capacity = ext_count;
  if (!Ok(xrEnumerateInstanceExtensionProperties(nullptr, ext_capacity, &ext_count,
                                                 exts.data()),
          "xrEnumerateInstanceExtensionProperties") || ext_count > ext_capacity) return;
  exts.resize(ext_count);
  bool has_d3d11 = false, has_display_refresh = false;
  for (const auto& e : exts) {
    if (!strcmp(e.extensionName, XR_KHR_D3D11_ENABLE_EXTENSION_NAME))
      has_d3d11 = true;
    if (!strcmp(e.extensionName, XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME))
      has_display_refresh = true;
  }
  if (!has_d3d11) {
    DSVR_ERROR("XR: runtime lacks XR_KHR_D3D11_enable -- cannot bind D3D11. "
               "Try SteamVR/Link if using another runtime. VR skipped.");
    return;
  }

  XrInstanceCreateInfo create{XR_TYPE_INSTANCE_CREATE_INFO};
  create.applicationInfo = app;
  const char* enabled[] = {XR_KHR_D3D11_ENABLE_EXTENSION_NAME,
                           XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME};
  create.enabledExtensionNames = enabled;
  create.enabledExtensionCount = has_display_refresh ? 2 : 1;
  if (!Ok(xrCreateInstance(&create, &g_instance), "xrCreateInstance")) return;
  g_get_display_refresh_rate = nullptr;
  g_display_refresh_hz = 0.0;
  if (has_display_refresh) {
    PFN_xrVoidFunction query = nullptr;
    if (XR_SUCCEEDED(xrGetInstanceProcAddr(g_instance, "xrGetDisplayRefreshRateFB", &query)))
      g_get_display_refresh_rate = reinterpret_cast<PFN_xrGetDisplayRefreshRateFB>(query);
    if (!g_get_display_refresh_rate)
      DSVR_WARN("XR: advertised display refresh query unavailable; using predicted period");
  }
  DSVR_LOG("XR: instance created (physical refresh observation %s; FPS Unlock dependency owns app pacing)",
           g_get_display_refresh_rate ? "enabled" : "unavailable");

  XrInstanceProperties props{XR_TYPE_INSTANCE_PROPERTIES};
  if (XR_SUCCEEDED(xrGetInstanceProperties(g_instance, &props)))
    DSVR_LOG("XR: runtime '%s' version %u.%u.%u", props.runtimeName,
             XR_VERSION_MAJOR(props.runtimeVersion),
             XR_VERSION_MINOR(props.runtimeVersion),
             XR_VERSION_PATCH(props.runtimeVersion));

  }

  XrSystemGetInfo sysinfo{XR_TYPE_SYSTEM_GET_INFO};
  sysinfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
  if (!Ok(xrGetSystem(g_instance, &sysinfo, &g_system), "xrGetSystem")) return;

  XrSystemProperties sysprops{XR_TYPE_SYSTEM_PROPERTIES};
  if (XR_SUCCEEDED(xrGetSystemProperties(g_instance, g_system, &sysprops))) {
    DSVR_LOG("XR: system '%s' vendor=%u max-layer=%u orient=%d pos=%d",
             sysprops.systemName, sysprops.vendorId,
             sysprops.graphicsProperties.maxLayerCount,
             sysprops.trackingProperties.orientationTracking,
             sysprops.trackingProperties.positionTracking);
  }

  // Recommended eye resolution (drives render scale in P2c).
  uint32_t view_count = 0;
  if (Ok(xrEnumerateViewConfigurationViews(
             g_instance, g_system,
             XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &view_count,
             nullptr),
         "xrEnumerateViewConfigurationViews(count)") &&
      view_count == 2) {
    std::vector<XrViewConfigurationView> views(
        view_count, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    const auto view_capacity = view_count;
    const auto view_result = xrEnumerateViewConfigurationViews(
        g_instance, g_system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
        view_count, &view_count, views.data());
    if (XR_FAILED(view_result) || view_count != 2 || view_count > view_capacity) {
      DSVR_WARN("XR: invalid eye configuration; deferring XR setup");
      return;
    }
    // Log EVERY eye and size the shared eye buffers to the largest
    // recommendation: HMDs are not required to give both eyes the same
    // resolution or FOV, and nothing in DSVR may assume a specific
    // headset. All rendering numbers come from the runtime (this call
    // plus per-frame xrLocateViews FOVs), never from a device spec.
    for (uint32_t i = 0; i < view_count; ++i)
      DSVR_LOG("XR: view %u recommended %ux%u (max %ux%u) samples=%u/%u", i,
               views[i].recommendedImageRectWidth,
               views[i].recommendedImageRectHeight,
               views[i].maxImageRectWidth, views[i].maxImageRectHeight,
               views[i].recommendedSwapchainSampleCount,
               views[i].maxSwapchainSampleCount);
    for (unsigned eye = 0; eye < 2; ++eye) {
      g_eyes[eye].recommended_width = views[eye].recommendedImageRectWidth;
      g_eyes[eye].recommended_height = views[eye].recommendedImageRectHeight;
      g_eyes[eye].max_width = views[eye].maxImageRectWidth;
      g_eyes[eye].max_height = views[eye].maxImageRectHeight;
    }
  } else {
    DSVR_WARN("XR: primary stereo requires exactly two runtime view recommendations");
    return;
  }

  // D3D11 requirements must be queried before session creation.
  auto get_requirements =
      (PFN_xrGetD3D11GraphicsRequirementsKHR)nullptr;
  xrGetInstanceProcAddr(g_instance, "xrGetD3D11GraphicsRequirementsKHR",
                        (PFN_xrVoidFunction*)&get_requirements);
  if (get_requirements) {
    XrGraphicsRequirementsD3D11KHR req{
        XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
    if (Ok(get_requirements(g_instance, g_system, &req),
           "xrGetD3D11GraphicsRequirementsKHR")) {
      DSVR_LOG("XR: D3D11 min feature level 0x%x, adapter LUID %ld:%lu",
               (unsigned)req.minFeatureLevel,
               (long)req.adapterLuid.HighPart,
               (unsigned long)req.adapterLuid.LowPart);
    }
  }

  XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
  binding.device = g_device;
  XrSessionCreateInfo session_create{XR_TYPE_SESSION_CREATE_INFO};
  session_create.next = &binding;
  session_create.systemId = g_system;
  if (!Ok(xrCreateSession(g_instance, &session_create, &g_session),
          "xrCreateSession"))
    return;
  QueryDisplayRefresh();
  DSVR_LOG("XR: D3D11 session created (waiting for READY via Present poll)");
  InputAttachSession(g_instance, g_session);  // no-op unless P4 enabled
}

void XrOnPresentFrame() {
  if (g_xr_thread && g_xr_thread != GetCurrentThreadId()) return;
  if (g_session)
    PollEvents();
  else if (g_device)
    XrOnDeviceCaptured(g_device);  // throttled retry until session exists
  if (g_destroy_pending && !DestroySwapchains()) { InvalidateTracking(); return; }
  if (g_native_frame.Active() && GetTickCount64() - g_native_begin_ms > 100) {
    g_native_frame.Cancel();
    InvalidateTracking();
  }
  if (!g_native_frame.Active()) g_native_frame.Drain();
  ServiceInactiveFrames();
}

bool XrBeginNativeStereoFrame(void* reference_texture2d, NativeStereoFrame& out) {
  out = {};
  if (!reference_texture2d || !g_running.load() || !g_session || !g_device ||
      g_xr_thread != GetCurrentThreadId() || g_mono_in_frame ||
      g_native_frame.Active() || g_native_owns_present) return false;
  PollEvents();
  if (g_destroy_pending && !DestroySwapchains()) { InvalidateTracking(); return false; }
  // VISIBLE renders too: shouldRender is true with or without input focus.
  if (!GraphicsFrameReady() || (g_state != XR_SESSION_STATE_FOCUSED &&
      g_state != XR_SESSION_STATE_VISIBLE)) {
    ServiceInactiveFrames();return false;
  }
  if (!DrainMonoImages()) { InvalidateTracking(); return false; }
  auto* reference = static_cast<ID3D11Texture2D*>(reference_texture2d);
  ID3D11Device* source_device = nullptr;
  reference->GetDevice(&source_device);
  const bool same_device = source_device == g_device;
  if (source_device) source_device->Release();
  if (!same_device || !EnsureSwapchains(reference) || !g_local_space) return false;
  const auto before = g_native_frame.Token();
  const bool begun = g_native_frame.Begin(g_session, g_space, g_head_space);
  if (g_native_frame.Token() != before) g_native_owns_present = true;
  if (!begun) { InvalidateTracking(); return false; }
  g_native_begin_ms = GetTickCount64();
  if (g_space_change_pending &&
      g_native_frame.State().predictedDisplayTime >= g_space_change_time) {
    InvalidateTracking(true);
    g_space_change_pending = false;
  }
  if (!g_native_frame.Acquire(g_eyes[0].swapchain, g_eyes[1].swapchain)) {
    g_native_frame.Cancel();
    InvalidateTracking();
    return false;
  }
  PublishRate(g_native_frame.State().predictedDisplayPeriod);
  NativeStereoFrame prediction{};
  prediction.token = g_native_frame.Token();
  prediction.predicted_time_ns = g_native_frame.State().predictedDisplayTime;
  prediction.color_format = g_eye_format;
  TrackingFrame tracking{};
  tracking.focused = tracking.orientation_valid = tracking.position_valid = true;
  tracking.predicted_time_ns = prediction.predicted_time_ns;
  tracking.display_period_ns = g_native_frame.State().predictedDisplayPeriod;
  for (unsigned eye = 0; eye < 2; ++eye) {
    const auto index = g_native_frame.ImageIndex(eye);
    if (index >= g_eyes[eye].images.size() || !g_eyes[eye].images[index].texture) {
      g_native_frame.Cancel(); InvalidateTracking(); return false;
    }
    auto& target = prediction.eyes[eye];
    target.width = g_eyes[eye].width;
    target.height = g_eyes[eye].height;
    target.texture2d = g_eyes[eye].images[index].texture;
    const auto& view = g_native_frame.View(eye);
    const auto& q = view.pose.orientation;
    const auto& p = view.pose.position;
    target.orientation[0] = q.x; target.orientation[1] = q.y;
    target.orientation[2] = q.z; target.orientation[3] = q.w;
    target.position[0] = p.x; target.position[1] = p.y; target.position[2] = p.z;
    target.fov[0] = view.fov.angleLeft; target.fov[1] = view.fov.angleRight;
    target.fov[2] = view.fov.angleUp; target.fov[3] = view.fov.angleDown;
    const float vertical = target.fov[2] - target.fov[3];
    if (vertical > tracking.vfov_rad) {
      tracking.vfov_rad = vertical;
      tracking.fov_aspect = (std::tan(target.fov[1]) - std::tan(target.fov[0])) /
                           (std::tan(target.fov[2]) - std::tan(target.fov[3]));
    }
  }
  if (prediction.eyes[0].texture2d == prediction.eyes[1].texture2d) {
    g_native_frame.Cancel(); InvalidateTracking(); return false;
  }
  const auto& head = g_native_frame.HeadPose();
  tracking.head_quat[0] = head.orientation.x;
  tracking.head_quat[1] = head.orientation.y;
  tracking.head_quat[2] = head.orientation.z;
  tracking.head_quat[3] = head.orientation.w;
  tracking.head_pos[0] = head.position.x;
  tracking.head_pos[1] = head.position.y;
  tracking.head_pos[2] = head.position.z;
  InputPollFrame(g_space, tracking.head_pos,
                 g_tracking_frames.CurrentEpoch(),
                 prediction.predicted_time_ns);
  InputCopyTrackingSamples(tracking.hand, tracking.arm);
  tracking.captured_ms = GetTickCount64();
  prediction.tracking_sequence = g_tracking_frames.Publish(tracking);
  if (!g_tracking_frames.Read(tracking.captured_ms, 100, tracking)) {
    g_native_frame.Cancel(); InvalidateTracking(); return false;
  }
  prediction.space_epoch = tracking.space_epoch;
  SampleCalibration(tracking);
  g_native_prediction = prediction;
  out = prediction;
  return true;
}

bool XrEndNativeStereoFrame(std::uint64_t token, unsigned rendered_eye_mask) {
  if (g_xr_thread != GetCurrentThreadId() || !g_native_frame.Active() ||
      !token || token != g_native_frame.Token()) return false;
  PollEvents();
  TrackingFrame tracking{};
  if (!g_native_frame.Active() || rendered_eye_mask != 3 || !g_running.load() ||
      (g_state != XR_SESSION_STATE_FOCUSED &&
       g_state != XR_SESSION_STATE_VISIBLE) ||
      GetTickCount64() - g_native_begin_ms > 100 ||
      !g_tracking_frames.Read(GetTickCount64(), 100, tracking) ||
      tracking.sequence != g_native_prediction.tracking_sequence ||
      tracking.space_epoch != g_native_prediction.space_epoch) {
    g_native_frame.Cancel(); InvalidateTracking(); return false;
  }
  const uint32_t widths[2]{g_native_prediction.eyes[0].width, g_native_prediction.eyes[1].width};
  const uint32_t heights[2]{g_native_prediction.eyes[0].height, g_native_prediction.eyes[1].height};
  const bool submitted = g_native_frame.Submit(widths, heights);
  g_native_prediction = {};
  if (!submitted) InvalidateTracking();
  return submitted;
}

void XrSubmitMonoFrame(void* backbuffer_texture2d) {
  if (!g_running.load() || !g_session || !g_device || !backbuffer_texture2d)
    return;
  if (g_xr_thread != GetCurrentThreadId() || g_mono_in_frame) return;
  PollEvents();
  if(!GraphicsFrameReady()){g_comfort_vignette.Reset();ServiceInactiveFrames();return;}
  if (g_destroy_pending && !DestroySwapchains()) { InvalidateTracking(); return; }
  if (g_native_owns_present) { g_native_owns_present = false; return; }
  if (g_native_frame.Busy()) return;
  g_mono_in_frame = true;
  struct MonoGuard { ~MonoGuard() { g_mono_in_frame = false; } } mono_guard;
  D3D11StateGuard native_state(g_context);
  const auto frame_start = QpcNow();
  RenderMetrics metrics{};
  metrics.frame_sequence = ++g_metrics_sequence;
  struct MetricsGuard {
    RenderMetrics& metrics;
    long long started;
    ~MetricsGuard() {
      metrics.cpu_frame_ms = QpcElapsedMs(QpcNow() - started);
      const auto& gpu = g_gpu_timer.Latest();
      metrics.gpu_frame_sequence = gpu.sequence;
      metrics.gpu_copy_ms = gpu.copy_ms;
      metrics.gpu_blit_ms = gpu.blit_ms;
      metrics.gpu_valid = gpu.sequence != 0;
      metrics.image_timeouts=g_mono_image_timeouts;
      metrics.held_image_skips=g_mono_lease_skips;
      std::lock_guard<std::mutex> lock(g_metrics_mutex);
      g_metrics_snapshot = metrics;
    }
  } metrics_guard{metrics, frame_start};
  if (!g_running.load()) return;
  if (!DrainMonoImages()) {g_comfort_vignette.Reset();++g_mono_lease_skips;return;} // fresh pose retains its normal age limit
  auto* presented_image = static_cast<ID3D11Texture2D*>(backbuffer_texture2d);
  auto* scene_source = DepthProbeSceneSource();
  auto* backbuffer = scene_source ? scene_source
      : g_stage_from_scene && g_stage ? g_stage : presented_image;
  std::uint64_t camera_sequence_at_entry = 0;
  DWORD camera_thread_at_entry = 0;
  TrackingFrame camera_frame_at_entry;
  {
    std::lock_guard<std::mutex> lock(g_camera_tag_mutex);
    camera_sequence_at_entry = g_camera_sequence;
    camera_thread_at_entry = g_camera_thread;
    camera_frame_at_entry = g_camera_frame;
  }

  const bool retaining_source = !scene_source && g_stage_from_scene && g_stage;
  if (retaining_source) {
    camera_frame_at_entry = g_completed_camera_frame;
    camera_sequence_at_entry = g_completed_camera_frame.sequence;
  }
  metrics.source_retained = retaining_source;
  metrics.source_age_ms = retaining_source && g_source_capture_ms
      ? GetTickCount64()-g_source_capture_ms : 0;
  metrics.source_generation = g_source_generation;

  XrFrameWaitInfo wait_info{XR_TYPE_FRAME_WAIT_INFO};
  XrFrameState state{XR_TYPE_FRAME_STATE};
  const long long wait_start = QpcNow();
  if (!Ok(xrWaitFrame(g_session, &wait_info, &state), "xrWaitFrame")) {
    InvalidateTracking();
    return;
  }
  const double wait_ms = QpcElapsedMs(QpcNow() - wait_start);
  metrics.xr_wait_ms = wait_ms;
  PublishRate(state.predictedDisplayPeriod);
  if (g_space_change_pending && state.predictedDisplayTime >= g_space_change_time) {
    InvalidateTracking(true);
    g_space_change_pending = false;
  }
  XrFrameBeginInfo begin_info{XR_TYPE_FRAME_BEGIN_INFO};
  if (!Ok(xrBeginFrame(g_session, &begin_info), "xrBeginFrame")) {
    InvalidateTracking();
    return;
  }

  XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
  XrCompositionLayerProjectionView views_out[2]{};
  const XrCompositionLayerBaseHeader* layers[1] = {
      (const XrCompositionLayerBaseHeader*)&layer};
  bool render = false;
  bool published = false;
  bool tracking_failed = false;
  SymmetricFrustum mono_projection{};
  std::uint64_t published_sequence = 0;
  std::uint64_t published_epoch = 0;

  if (state.shouldRender && EnsureSwapchains(presented_image)) {
    const bool eye_processing_ready[2]{EnsureEyeProcessingSurfaces(0), EnsureEyeProcessingSurfaces(1)};
    if (!eye_processing_ready[0] || !eye_processing_ready[1]) {
      static std::atomic<bool> post_surface_logged{false};
      if (!post_surface_logged.exchange(true))
        DSVR_WARN("XR: full-eye post surfaces unavailable; rendering directly to the OpenXR swapchains");
    }
    // Only a failed tracking attempt invalidates tracking. Resource/image
    // failures and shouldRender=false retain the last pose until its normal
    // 100 ms freshness deadline; they must not flash the native camera/body.
    tracking_failed = true;
    XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
    locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locate.displayTime = state.predictedDisplayTime;
    locate.space = g_space;
    XrViewState view_state{XR_TYPE_VIEW_STATE};
    XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
    uint32_t view_count = 0;
    if (Ok(xrLocateViews(g_session, &locate, &view_state, 2, &view_count,
                         views),
           "xrLocateViews") &&
        view_count == 2) {
      XrPosef head{};
      const bool head_valid = XrStereoFrame::LocateHeadPose(
          g_head_space, g_space, state.predictedDisplayTime, head);
      if (!g_views_logged) {
        g_views_logged = true;
        // Per-eye runtime truth, logged once: any HMD/runtime change is
        // visible here instead of being silently Quest-shaped.
        for (int eye = 0; eye < 2; ++eye)
          DSVR_LOG(
              "XR: eye%d pos=%.3f,%.3f,%.3f fov(rad) l=%.3f r=%.3f u=%.3f "
              "d=%.3f (h=%.1fdeg v=%.1fdeg)",
              eye, views[eye].pose.position.x, views[eye].pose.position.y,
              views[eye].pose.position.z, views[eye].fov.angleLeft,
              views[eye].fov.angleRight, views[eye].fov.angleUp,
              views[eye].fov.angleDown,
              (views[eye].fov.angleRight - views[eye].fov.angleLeft) *
                  57.29578f,
              (views[eye].fov.angleUp - views[eye].fov.angleDown) *
                  57.29578f);
        DSVR_LOG("XR: viewState flags=0x%llx",
                 (unsigned long long)view_state.viewStateFlags);
      }
      const CalibrationStatus calibration_status = CalibrationGetStatus();
      const bool calibration_room = calibration_status.screen_visible;
      bool recovery_room = !calibration_room && retaining_source && metrics.source_age_ms > 500;
      const bool settings_room = ModMenuSettingsOpen() && g_local_space;
      const bool orientation_valid =
          (view_state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0 &&
          XrStereoFrame::ValidPose(views[0].pose) && XrStereoFrame::ValidPose(views[1].pose);
      const bool position_valid =
          (view_state.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) != 0 &&
          XrStereoFrame::ValidPose(views[0].pose) && XrStereoFrame::ValidPose(views[1].pose);
      Config presentation_cfg = DiagnosticsCurrentConfig();
      const bool title_scene = CameraIsTitle();
      const bool game_camera_ready = CameraGameplayReady();
      const bool cutscene = CameraNativeCutscene();
      const MenuState raw_menu = calibration_room
          ? MenuState::CalibrationRoom
          : (presentation_cfg.theater_override || g_theater_override.load() ||
             ModMenuVisible())
                ? MenuState::ReadingMenu
                : recovery_room ? MenuState::Loading
                : title_scene ? MenuState::FrontEnd
                : (!game_camera_ready || cutscene) ? MenuState::Loading
                                                   : MenuState::Gameplay;
      const bool presentation_fallback = raw_menu != MenuState::Gameplay ||
          !presentation_cfg.headlook_enabled || presentation_cfg.gameplay_theater;
      // A visible calibration/menu/loading surface is composited safely even
      // while positional tracking settles. Immersive game frames still require
      // a current valid camera/view pose below.
      render = presentation_fallback ||
               (orientation_valid && (head_valid || position_valid));
      XrView display_views[2] = {views[0], views[1]};
      const XrFovf safe_fov{-0.7853982f, 0.7853982f,
                            0.7853982f, -0.7853982f};
      if (orientation_valid) g_calibration_last_view_valid = true;
      for (int eye = 0; eye < 2; ++eye) {
        const auto& fov = views[eye].fov;
        const bool fov_valid = XrStereoFrame::ValidView(views[eye]);
        if (orientation_valid) {
          g_calibration_last_eye_orientation[eye] =
              views[eye].pose.orientation;
          if (fov_valid) {
            g_calibration_last_eye_fov[eye] = fov;
            g_calibration_last_eye_fov_valid[eye] = true;
            g_calibration_last_view_valid = true;
          }
        } else {
          display_views[eye].pose.orientation = g_calibration_last_view_valid
              ? g_calibration_last_eye_orientation[eye]
              : XrQuaternionf{0, 0, 0, 1};
          display_views[eye].fov = fov_valid ? fov
              : g_calibration_last_eye_fov_valid[eye]
                    ? g_calibration_last_eye_fov[eye] : safe_fov;
        }
        if (!fov_valid && orientation_valid)
          display_views[eye].fov = g_calibration_last_eye_fov_valid[eye]
              ? g_calibration_last_eye_fov[eye] : safe_fov;
      }
      const unsigned long long gen_before =
          g_menu_debounce.Current().generation;
      const MenuSnapshot snap =
          g_menu_debounce.Feed(raw_menu, (long long)GetTickCount64());
      const bool title = snap.state == MenuState::FrontEnd;
      const bool menu = MenuStateWantsPanel(snap.state);
      const bool theater = MenuStateWantsCanvas(snap.state);
      // The calibration poster, settings shrine, and 16:9 game theater are
      // world surfaces in the same eye image, so the final AA/CAS pass can
      // process every visible element together.
      bool theater_room =
          !calibration_room &&
          (settings_room || !presentation_cfg.headlook_enabled ||
           presentation_cfg.gameplay_theater || theater) && g_local_space;
      bool immersive_room = calibration_room || theater_room;
      if (snap.generation != gen_before) {
          DSVR_LOG("XR: panel %s (debounced state=%d gen=%llu); "
                 "in-game automatic detection pending",
                 menu ? "open" : "closed", (int)snap.state,
                   snap.generation);
      }
      g_menu_active.store(menu);
      if (g_recenter_menu.exchange(false)) {
        g_menu_anchored = false;
        g_calibration_anchored = false;
      }
      // The panel is horizon-locked in the reference space: it keeps its
      // place across menu open/close. It only loses the anchor while the
      // session is immersive (no panel shown) or the space goes invalid.
      if (!theater_room) g_menu_anchored = false;
      if (!calibration_room) g_calibration_anchored = false;
      // Tangent extents of the world-anchored 16:9 theater surface. The room
      // shader, pointer ray, and displayed game image share one angular size.
      ScreenTangentExtents panel_extents{};
      {
        Config cfg = DiagnosticsCurrentConfig();
        {
          float half_h=0, half_v=0;
          for (const auto& view : display_views) {
            half_h=std::max({half_h, -std::tan(view.fov.angleLeft), std::tan(view.fov.angleRight)});
            half_v=std::max({half_v, std::tan(view.fov.angleUp), -std::tan(view.fov.angleDown)});
          }
          g_vfov_rad=2.0f*std::atan(half_v);
          g_fov_aspect=half_h/half_v;
          if (!g_fov_logged) {
            g_fov_logged=true;
            DSVR_LOG("XR: centered source covers both eye frusta vfov=%.1fdeg aspect=%.3f", g_vfov_rad*57.29578f, g_fov_aspect);
          }
        }
        g_immersive = cfg.headlook_enabled && !cfg.gameplay_theater && !menu;
        if (g_immersive) {
          const float immersive_vfov = camera_frame_at_entry.sequence
              ? camera_frame_at_entry.vfov_rad
              : g_vfov_rad * std::clamp(cfg.fov_scale, 0.7f, 1.3f);
          float tan_v = std::tan(immersive_vfov * 0.5f);
          float tan_h_game = tan_v * (camera_frame_at_entry.sequence
              ? camera_frame_at_entry.fov_aspect : g_fov_aspect);
          const SymmetricFrustum game{tan_h_game, tan_v, immersive_vfov,
                camera_frame_at_entry.sequence ? camera_frame_at_entry.fov_aspect
                                               : g_fov_aspect};
          mono_projection = game;
        } else {
        // Virtual-screen rects from each eye's (asymmetric) native FOV:
        // map desired tangent extents to pixel fractions of the eye buffer.
        // Trigger held + right stick up/down resizes the window live: the
          // stick Y nudges the FOV while the trigger is down, clamped to the
          // same 30..110 deg range as the config slider. The stick vector
          // lives in the pad bridge (VrPadHand), not the hand sample.
          float theater_fov_deg = cfg.mono_screen_fov_deg;
          {
            VirtualPadState pad_now{};
              float stick_y = 0.0f;
              bool trigger_down = false;
              if (InputLatestPad(pad_now)) {
              stick_y = pad_now.thumb_ry / 32767.0f;
                trigger_down = pad_now.right_trigger > 128;
              }
              if (trigger_down && std::isfinite(stick_y) &&
                std::fabs(stick_y) > 0.25f) {
                theater_fov_deg = std::clamp(
                    theater_fov_deg + stick_y * 30.0f * 0.016f, 30.0f, 110.0f);
              if (theater_fov_deg != cfg.mono_screen_fov_deg) {
                  Config live = cfg;
                  live.mono_screen_fov_deg = theater_fov_deg;
                DiagnosticsRequestConfig(live, true);
              }
            }
          }
          const EyeFrustum theater_eyes[2] = {
              FrustumFromAngles(display_views[0].fov.angleLeft,
                                display_views[0].fov.angleRight,
                                display_views[0].fov.angleUp,
                                display_views[0].fov.angleDown),
              FrustumFromAngles(display_views[1].fov.angleLeft,
                                display_views[1].fov.angleRight,
                                display_views[1].fov.angleUp,
                                display_views[1].fov.angleDown)};
          // The world surface stays 16:9. The source is contained inside it,
          // with the pointer mapped through the same centered letterbox.
          // g_stage_w/h are only filled by PrepareMonoSource later in this
          // same frame, so use the backbuffer description directly.
          D3D11_TEXTURE2D_DESC theater_source{};
          backbuffer->GetDesc(&theater_source);
            const auto requested_screen = TheaterScreenExtents(
              theater_fov_deg * 0.0174532925f, kTheaterAspect);
            const auto eye_fitted = FitTheaterScreenToEyes(
              requested_screen, theater_eyes, kTheaterAspect);
          const auto theater_screen = eye_fitted;
          panel_extents = theater_screen;
          for (int eye = 0; eye < 2; ++eye) {
              const PixelRect raw_rect = TheaterRectForEye(
                  theater_screen, theater_eyes[eye], (float)g_eyes[eye].width,
                  (float)g_eyes[eye].height);
              const PixelRect rect = FitPixelRectToAspect(
                  raw_rect, kTheaterAspect,
                  (float)std::min(g_eyes[eye].width, theater_source.Width),
                  (float)std::min(g_eyes[eye].height, theater_source.Height));
              if (eye == 0 && raw_rect.w > 0.0f && raw_rect.h > 0.0f &&
                  rect.w > 0.0f && rect.h > 0.0f) {
                const float pixel_scale = std::min(rect.w / raw_rect.w,
                                                   rect.h / raw_rect.h);
                panel_extents.tan_half_h *= pixel_scale;
                panel_extents.tan_half_v *= pixel_scale;
              }
              g_screen_rect[eye][0] = rect.x;
              g_screen_rect[eye][1] = rect.y;
              g_screen_rect[eye][2] = rect.w;
              g_screen_rect[eye][3] = rect.h;
          }
          g_theater_pointer_fov_rad.store(
              2.0f * std::atan(panel_extents.tan_half_h),
              std::memory_order_relaxed);
        }  // immersive vs theater rect
        if (theater_room) {
          float width = 2.0f * presentation_cfg.menu_distance_m *
                        panel_extents.tan_half_h;
          if (!(width > 0.0f) || !std::isfinite(width))
            width = MenuWidth(presentation_cfg.menu_distance_m,
                title ? 60.0f : presentation_cfg.menu_reading_fov_deg);
          g_panel_width.store(width, std::memory_order_relaxed);
        }
        // Theater reuses a centered 16:9 subrect of the left swapchain.
        // Publish the pose for the game-thread camera hook (camhook.cpp).
        // Writing CameraMan here on the render thread was overwritten by
        // the game's own camera update before the next render ("panel
        // welded to my face"), so application moved to the hook.
        TrackingFrame frame{};
        frame.predicted_time_ns = state.predictedDisplayTime;
        frame.display_period_ns = state.predictedDisplayPeriod;
        frame.focused = g_state == XR_SESSION_STATE_FOCUSED;
        frame.orientation_valid = render;
        frame.position_valid =
            (view_state.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) != 0;
        frame.head_tracking_flags_available = true;
        frame.head_orientation_tracked =
            (view_state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_TRACKED_BIT) != 0;
        frame.head_position_tracked =
            (view_state.viewStateFlags & XR_VIEW_STATE_POSITION_TRACKED_BIT) != 0;
        frame.vfov_rad = g_vfov_rad;
        frame.fov_aspect = g_fov_aspect;
        const auto& hp = head;
        frame.head_quat[0] = hp.orientation.x;
        frame.head_quat[1] = hp.orientation.y;
        frame.head_quat[2] = hp.orientation.z;
        frame.head_quat[3] = hp.orientation.w;
        frame.head_pos[0] = hp.position.x;
        frame.head_pos[1] = hp.position.y;
        frame.head_pos[2] = hp.position.z;
        // P4 poll shares this frame's predicted time + reference space;
        // inert (one atomic read) until actions are attached.
        InputPollFrame(g_space, frame.head_pos,
                       g_tracking_frames.CurrentEpoch(),
                       (long long)state.predictedDisplayTime);
        InputCopyTrackingSamples(frame.hand, frame.arm);
        frame.captured_ms = GetTickCount64();
        // Publish on pose validity, not input focus: VISIBLE sessions still
        // render (shouldRender), so the camera pipeline stays primed and the
        // image restores on the first focused frame instead of reconverging.
        if (frame.orientation_valid && frame.position_valid) {
          published_sequence = g_tracking_frames.Publish(frame);
          published = true;
          metrics.tracking_published = true;
          tracking_failed = false;
          TrackingFrame tagged{};
          if (g_tracking_frames.Read(frame.captured_ms, 100, tagged)) {
            SampleCalibration(tagged);
            published_epoch = tagged.space_epoch;
            VrTraceEvent trace{};
            trace.event = "tracking";
            trace.seq = tagged.sequence;
            trace.epoch = tagged.space_epoch;
            trace.predicted_ns = tagged.predicted_time_ns;
            trace.period_ns = tagged.display_period_ns;
            for (unsigned j = 0; j < 4; ++j) trace.values[j] = tagged.head_quat[j];
            for (unsigned j = 0; j < 3; ++j) trace.values[j + 4] = tagged.head_pos[j];
            VrTraceRecord(trace);
          }
        }
        (void)cfg;
      }
      // The native scene predates this xrWaitFrame/LocateViews call. Submit
      // its camera-bound center pose, while still publishing the NEW prediction
      // for the next natural game update. Missing/stale provenance drops the
      // image, never the fresh tracking packet needed to recover next frame.
      metrics.target_tracking_sequence = published_sequence;
      if (g_immersive) {
        const auto& source = camera_frame_at_entry;
        const auto now = GetTickCount64();
        const XrPosef source_pose{{source.head_quat[0],source.head_quat[1],source.head_quat[2],source.head_quat[3]},
            {source.head_pos[0],source.head_pos[1],source.head_pos[2]}};
        MonoProvenance provenance;
        provenance.sequence = source.sequence;
        provenance.expected_sequence = camera_sequence_at_entry;
        provenance.space_epoch = source.space_epoch;
        provenance.current_epoch = g_tracking_frames.CurrentEpoch();
        provenance.session_focused = g_state == XR_SESSION_STATE_FOCUSED;
        provenance.source_focused = source.focused;
        provenance.orientation_valid = source.orientation_valid;
        provenance.position_valid = source.position_valid;
        provenance.pose_valid = XrStereoFrame::ValidPose(source_pose);
        provenance.now_ms = now;
        provenance.captured_ms = source.captured_ms;
        provenance.predicted_ns = static_cast<std::int64_t>(source.predicted_time_ns);
        provenance.display_time_ns = static_cast<std::int64_t>(state.predictedDisplayTime);
        provenance.display_period_ns = static_cast<std::int64_t>(state.predictedDisplayPeriod);
        provenance.vfov_rad = source.vfov_rad;
        provenance.fov_aspect = source.fov_aspect;
        if (const char* gate = MonoProvenanceFailure(provenance)) {
          // One line per second names the gate that took the view away: a
          // silent room looks like a stuck overlay in the headset.
          static unsigned long long last_recovery_log_ms = 0;
          if (last_recovery_log_ms == 0 || now - last_recovery_log_ms >= 1000) {
            last_recovery_log_ms = now;
            DSVR_LOG("XR: recovery room gate=%s seq=%llu expected=%llu age_ms=%lld",
                     gate, static_cast<unsigned long long>(source.sequence),
                     static_cast<unsigned long long>(camera_sequence_at_entry),
                     static_cast<long long>(now) -
                         static_cast<long long>(source.captured_ms));
          }
          recovery_room = true;
          theater_room = immersive_room = true;
          g_immersive = false;
          g_menu_active.store(true);
          render = true;
        }
        else metrics.source_tracking_sequence = source.sequence;
      }
      // Snapshot the source once, before either eye can consume it. Both eye
      // draws sample this same frame, including format conversion/aspect fit.
      D3D11_TEXTURE2D_DESC src_desc{};
      backbuffer->GetDesc(&src_desc);
      XrView calibration_views[2] = {display_views[0], display_views[1]};
      for (int eye = 0; eye < 2; ++eye) {
        if (position_valid) {
          g_calibration_last_eye_position[eye] = views[eye].pose.position;
          g_calibration_last_eye_position_valid = true;
        } else if (g_calibration_last_eye_position_valid) {
          calibration_views[eye].pose.position =
              g_calibration_last_eye_position[eye];
        } else if (head_valid) {
          calibration_views[eye].pose.position = head.position;
          const auto right = QuatRotate({1, 0, 0}, head.orientation.x,
              head.orientation.y, head.orientation.z, head.orientation.w);
          calibration_views[eye].pose.position.x += right.x * (eye ? 0.032f : -0.032f);
          calibration_views[eye].pose.position.y += right.y * (eye ? 0.032f : -0.032f);
          calibration_views[eye].pose.position.z += right.z * (eye ? 0.032f : -0.032f);
        } else {
          calibration_views[eye].pose.position = {
              eye ? 0.032f : -0.032f, 1.65f, 0.0f};
        }
      }
      if (immersive_room) {
        const auto profile = CalibrationCurrent();
        const float eye_height = profile.valid ? profile.eye_height_m
                                                : presentation_cfg.eye_height;
        bool stage_valid = false;
        if (g_floor_space) {
          XrSpaceLocation floor{XR_TYPE_SPACE_LOCATION};
          if (XR_SUCCEEDED(xrLocateSpace(g_floor_space, g_space,
                                         state.predictedDisplayTime, &floor)) &&
              (floor.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) &&
              std::isfinite(floor.pose.position.y)) {
            g_calibration_stage_pose = floor.pose;
            if (!(floor.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))
              g_calibration_stage_pose.orientation = {0, 0, 0, 1};
            XrExtent2Df bounds{};
            if (XR_SUCCEEDED(xrGetReferenceSpaceBoundsRect(
                    g_session, XR_REFERENCE_SPACE_TYPE_STAGE, &bounds)) &&
                std::isfinite(bounds.width) && std::isfinite(bounds.height) &&
                bounds.width >= 0.5f && bounds.height >= 0.5f &&
                bounds.width <= 1000.0f && bounds.height <= 1000.0f) {
              g_calibration_room_half_x = bounds.width * 0.5f;
              g_calibration_room_half_z = bounds.height * 0.5f;
              stage_valid = true;
            }
            if (!stage_valid) {
              g_calibration_room_half_x = 4.0f;
              g_calibration_room_half_z = 4.0f;
              stage_valid = true;
            }
          }
        }
        if (!stage_valid) {
          g_calibration_stage_pose = {};
          g_calibration_stage_pose.orientation.w = 1.0f;
          if (head_valid) {
            g_calibration_stage_pose.position = head.position;
            g_calibration_stage_pose.position.y -= eye_height;
          }
          g_calibration_room_half_x = 4.0f;
          g_calibration_room_half_z = 4.0f;
        }
        XrVector3f center{
            (calibration_views[0].pose.position.x +
             calibration_views[1].pose.position.x) * 0.5f,
            (calibration_views[0].pose.position.y +
             calibration_views[1].pose.position.y) * 0.5f,
            (calibration_views[0].pose.position.z +
             calibration_views[1].pose.position.z) * 0.5f};
        const auto& q = head_valid ? head.orientation
                                   : calibration_views[0].pose.orientation;
        const float fx = -2.0f * (q.x * q.z + q.w * q.y);
        const float fz = -(1.0f - 2.0f * (q.x * q.x + q.y * q.y));
        const float yaw = std::atan2(-fx, -fz);
        if (calibration_room && !g_calibration_anchored) {
          g_calibration_anchor = MakeHorizonAnchor(
              center.x, center.y, center.z, yaw,
              kCalibrationPanelDistanceM);
          g_calibration_anchored = true;
        }
        if (theater_room && !g_menu_anchored) {
          const float anchor_y = stage_valid
              ? g_calibration_stage_pose.position.y + eye_height : center.y;
          g_menu_anchor = MakeHorizonAnchor(
              center.x, anchor_y, center.z, yaw,
              presentation_cfg.menu_distance_m);
          g_menu_anchored = true;
        }
      }
      metrics.source_width = src_desc.Width;
      metrics.source_height = src_desc.Height;
      metrics.eye_width = g_eyes[0].width;
      metrics.eye_height = g_eyes[0].height;
      for (int eye = 0; eye < 2; ++eye) {
        metrics.eye_widths[eye] = g_eyes[eye].width;
        metrics.eye_heights[eye] = g_eyes[eye].height;
      }
      metrics.theater = theater_room;
      const float head_quat[4]{head.orientation.x, head.orientation.y,
                               head.orientation.z, head.orientation.w};
      const float vignette = g_comfort_vignette.Update(InputLatestComfortMotion(),
          head_quat, state.predictedDisplayTime, g_tracking_frames.CurrentEpoch(),
          render && published && g_immersive && presentation_cfg.comfort_vignette,
          presentation_cfg.vignette_strength);
      bool menu_surface_ready = false;
      bool source_ready = false;
      D3D11_TEXTURE2D_DESC menu_desc{};
      if (render) {
        g_gpu_timer.BeginFrame(g_device, g_context, metrics.frame_sequence);
        g_gpu_timer.BeginRange(g_context, 0);
        const auto copy_start = QpcNow();
        source_ready = PrepareMonoSource(backbuffer, src_desc);
        const bool source_copied = source_ready && !retaining_source;
        if (!source_ready) {
          recovery_room = true;
          theater_room = immersive_room = true;
          g_immersive = false;
          g_menu_active.store(true);
          if (g_stage && g_blit_srv) {
            backbuffer = g_stage;
            backbuffer->GetDesc(&src_desc);
            source_ready = true;
          }
          if (!g_menu_anchored) {
            const auto& pose = calibration_views[0].pose;
            const auto& q = pose.orientation;
            const float fx = -2.0f * (q.x*q.z + q.w*q.y);
            const float fz = -(1.0f - 2.0f * (q.x*q.x + q.y*q.y));
            g_menu_anchor = MakeHorizonAnchor(pose.position.x, pose.position.y,
                pose.position.z, std::atan2(-fx,-fz), presentation_cfg.menu_distance_m);
            g_menu_anchored = true;
          }
          metrics.theater = true;
          metrics.source_retained = source_ready;
          metrics.source_age_ms = g_source_capture_ms ? GetTickCount64()-g_source_capture_ms : 0;
        }
        metrics.source_width = source_ready ? g_stage_w : 0;
        metrics.source_height = source_ready ? g_stage_h : 0;
        if (source_copied && scene_source) {
          if (g_last_scene_source != reinterpret_cast<std::uintptr_t>(scene_source)) {
            ++g_source_generation;
            g_last_scene_source = reinterpret_cast<std::uintptr_t>(scene_source);
          }
          g_stage_from_scene = true;
          g_source_capture_ms = GetTickCount64();
          g_completed_camera_frame = camera_frame_at_entry;
          metrics.source_generation = g_source_generation;
        }
        if ((ModMenuVisible() || recovery_room) && EnsureMenuSurface()) {
          g_menu_surface->GetDesc(&menu_desc);
          menu_surface_ready = ModMenuRenderTexture(g_device, g_menu_surface, true,
              recovery_room ? "Waiting for a fresh game view. Keep your headset on; the view will return automatically." : nullptr);
        }
        render = source_ready || immersive_room;
        metrics.source_copy_cpu_ms = QpcElapsedMs(QpcNow() - copy_start);
        metrics.source_copies = source_copied ? 1 : 0;
        g_gpu_timer.EndRange(g_context, 0);
      }
      for (int eye = 0; eye < 2 && render; ++eye) {
        uint32_t index = 0;
        XrSwapchainImageAcquireInfo acquire{
            XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        const auto acquired = xrAcquireSwapchainImage(g_eyes[eye].swapchain, &acquire, &index);
        if (XR_SUCCEEDED(acquired)) g_mono_images[eye].chain = g_eyes[eye].swapchain;
        if (acquired != XR_SUCCESS) {
          Ok(acquired, "xrAcquireSwapchainImage");
          render = false;
          break;
        }
        XrSwapchainImageWaitInfo wait_image{
            XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wait_image.timeout = 5'000'000;
        const auto image_wait_start = QpcNow();
        const auto waited = xrWaitSwapchainImage(g_eyes[eye].swapchain, &wait_image);
        metrics.image_wait_ms += QpcElapsedMs(QpcNow() - image_wait_start);
        g_mono_images[eye].ready = XR_SUCCEEDED(waited) && waited != XR_TIMEOUT_EXPIRED;
        if (waited != XR_SUCCESS) {
          if(waited==XR_TIMEOUT_EXPIRED)++g_mono_image_timeouts;
          Ok(waited, "xrWaitSwapchainImage");
          render = false;
          break;
        }
        if (index >= g_eyes[eye].images.size() || index >= g_eyes[eye].rtvs.size() ||
            !g_eyes[eye].images[index].texture || !g_eyes[eye].rtvs[index]) {
          DSVR_ERROR("XR: eye%d returned invalid image index %u", eye, index);
          render = false;
          break;
        }
        g_gpu_timer.BeginRange(g_context, 1 + eye);
        const auto blit_start = QpcNow();
        const bool ui_room = calibration_room || settings_room || recovery_room;
        const bool room_panel_ready = ui_room ? menu_surface_ready
                                               : source_ready;
        if (immersive_room && room_panel_ready) {
          const D3D11_TEXTURE2D_DESC& panel_desc = ui_room ? menu_desc : src_desc;
          ID3D11ShaderResourceView* panel_srv = ui_room
              ? g_menu_surface_srv : g_blit_srv;
          const MenuAnchor& anchor = calibration_room
              ? g_calibration_anchor : g_menu_anchor;
          const float panel_width = calibration_room
              ? g_calibration_panel_width
              : g_panel_width.load(std::memory_order_relaxed);
          render = DrawImmersiveRoom(eye, index, calibration_views[eye],
              panel_desc, panel_srv, anchor, panel_width,
              calibration_room || settings_room ||
                  presentation_cfg.theater_floor_grid, ui_room);
        } else if (source_ready) {
          render = BlitToEye(eye, index, src_desc,
                             display_views[eye].fov, vignette, g_immersive ? &mono_projection : nullptr);
        } else if (immersive_room) {
          ID3D11RenderTargetView* target = g_eyes[eye].stage_rtv
              ? g_eyes[eye].stage_rtv : g_eyes[eye].rtvs[index];
          const float safe_room[4]{0.006f, 0.012f, 0.009f, 1.0f};
          if (target) g_context->ClearRenderTargetView(target, safe_room);
          render = target != nullptr;
        }
        if (!render && source_ready) {
          static std::atomic<bool> fallback_logged{false};
          if (!fallback_logged.exchange(true))
            DSVR_WARN("XR: immersive UI/theater pass unavailable; falling back to the complete game image");
          render = BlitToEye(eye, index, src_desc,
                             display_views[eye].fov, 0.0f, g_immersive ? &mono_projection : nullptr);
        }
        if (!render && immersive_room) {
          ID3D11RenderTargetView* target = g_eyes[eye].stage_rtv
              ? g_eyes[eye].stage_rtv : g_eyes[eye].rtvs[index];
          const float safe_room[4]{0.006f, 0.012f, 0.009f, 1.0f};
          if (target) {
            g_context->ClearRenderTargetView(target, safe_room);
            render = true;
          }
        }
        if (render && eye_processing_ready[eye] &&
            !ApplyFullImageEffects(eye, index, presentation_cfg)) {
          static std::atomic<bool> post_copy_logged{false};
          if (!post_copy_logged.exchange(true))
            DSVR_WARN("XR: post-processing unavailable; submitting the unprocessed eye image");
          g_context->CopyResource(g_eyes[eye].images[index].texture, g_eyes[eye].stage);
        }
        metrics.eye_blit_cpu_ms += QpcElapsedMs(QpcNow() - blit_start);
        if (render) ++metrics.eye_draws;
        g_gpu_timer.EndRange(g_context, 1 + eye);
        XrSwapchainImageReleaseInfo release{
            XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        const auto released = xrReleaseSwapchainImage(g_eyes[eye].swapchain, &release);
        if (XR_SUCCEEDED(released)) g_mono_images[eye] = {};
        if (released != XR_SUCCESS) {
          Ok(released, "xrReleaseSwapchainImage");
          render = false;
          break;
        }

        views_out[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
        views_out[eye].pose = immersive_room || !position_valid ||
                !orientation_valid
            ? calibration_views[eye].pose : views[eye].pose;
        if (g_immersive) {
          // Mono pixels originate at the center camera, including on canted
          // displays: neither eye's newer orientation/IPD rendered this image.
          const auto& source = camera_frame_at_entry;
          views_out[eye].pose.orientation = {source.head_quat[0],source.head_quat[1],
                                            source.head_quat[2],source.head_quat[3]};
          views_out[eye].pose.position = {source.head_pos[0],source.head_pos[1],source.head_pos[2]};
        }
        views_out[eye].fov = display_views[eye].fov;
        views_out[eye].subImage.swapchain = g_eyes[eye].swapchain;
        views_out[eye].subImage.imageRect.offset = {0, 0};
        views_out[eye].subImage.imageRect.extent = {(int32_t)g_eyes[eye].width,
                                                    (int32_t)g_eyes[eye].height};
        views_out[eye].subImage.imageArrayIndex = 0;
      }
      g_gpu_timer.EndFrame(g_context);
      if (render) {
        layer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
        layer.space = g_space;
        layer.viewCount = 2;
        layer.views = views_out;
      }
    }
  }

  if (tracking_failed) InvalidateTracking();
  if (!render) g_comfort_vignette.Reset();
  if (published && (published_sequence == 1 || published_sequence % 300 == 0))
    DSVR_LOG("XRTiming: submit_pose_seq=%llu camera_applied_seq_at_entry=%llu "
             "camera_tid=%lu render_tid=%lu predicted_ns=%lld wait_ms=%.3f "
             "(mono; rendered-view provenance NOT captured)",
             (unsigned long long)published_sequence,
             (unsigned long long)camera_sequence_at_entry,
             (unsigned long)camera_thread_at_entry,
             (unsigned long)GetCurrentThreadId(),
             (long long)state.predictedDisplayTime, wait_ms);
  XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
  end.displayTime = state.predictedDisplayTime;
  end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
  end.layerCount = render ? 1u : 0u;
  end.layers = render ? layers : nullptr;
  const auto end_start = QpcNow();
  const bool ended = Ok(xrEndFrame(g_session, &end), "xrEndFrame");
  metrics.end_frame_cpu_ms = QpcElapsedMs(QpcNow() - end_start);
  metrics.submitted = ended && render;
  VrTraceEvent trace{};
  trace.event = "submit";
  trace.seq = published_sequence;
  trace.epoch = published_epoch;
  trace.predicted_ns = state.predictedDisplayTime;
  trace.period_ns = state.predictedDisplayPeriod;
  trace.flags = (ended ? 1U : 0U) | (render ? 2U : 0U);
  // No frame/image generation is invented here; it remains zero until a
  // verified render-consumption adapter can prove the source image identity.
  VrTraceRecord(trace);
  if (!ended) InvalidateTracking();
  if (ended && render) {
    ++g_submitted;
    if (g_submitted == 1 || g_submitted % 600 == 0)
      DSVR_LOG("XR: mono frames submitted=%llu (same centered scene image "
               "in both eyes; native stereo renderer unbound)",
               g_submitted);
  }
}

bool XrSessionRunning() { return g_running.load(); }
bool XrSessionVisible() { return g_session_visible.load(); }
bool XrMenuPresentationActive() {
  // Every signal here is hard: no running session, a session the runtime is
  // not showing, a faulted frame pump, or the debounced front-end reading.
  // VISIBLE and FOCUSED both count as visible (PollEvents), so a focus blip
  // cannot flip this on its own; MenuDebouncer already carries the hysteresis
  // on the menu reading, and a missing camera sample must force theater at
  // once (loading and non-3D cutscenes are never immersive).
  return !g_running.load() || !g_session_visible.load() || g_pump_failed.load() || g_menu_active.load();
}
void XrToggleTheater() {
  bool old = g_theater_override.load();
  g_theater_override.store(!old);
}
void XrRecenterTheater() { g_recenter_menu.store(true); }

bool XrLatestHeadPose(float quat_xyzw[4], float pos_xyz[3]) {
  TrackingFrame frame{};
  if (!XrLatestTrackingFrame(frame)) return false;
  for (int i = 0; i < 4; ++i) quat_xyzw[i] = frame.head_quat[i];
  for (int i = 0; i < 3; ++i) pos_xyz[i] = frame.head_pos[i];
  return true;
}

bool XrLatestTrackingFrame(TrackingFrame& out) {
  return g_tracking_frames.Read(GetTickCount64(), 100, out);
}

MenuPointerState XrMenuPointerInput(unsigned display_width, unsigned display_height) {
  const Config cfg = DiagnosticsCurrentConfig();
  const bool running = g_running.load();
  const bool calibration_surface = running && g_menu_active.load() && g_space &&
      CalibrationGetStatus().screen_visible;
  const bool settings_surface = running && g_space && g_local_space &&
      ModMenuSettingsOpen();
  const bool menu_surface = running && g_space &&
      ((g_menu_active.load() && g_local_space) || calibration_surface ||
       settings_surface);
  const bool theater_screen = running && !menu_surface && g_space && (g_menu_active.load() || !cfg.headlook_enabled || cfg.gameplay_theater);
  const bool pointer_surface = menu_surface || theater_screen;
  TrackingFrame frame{};
  const bool fresh_frame = pointer_surface && XrLatestTrackingFrame(frame);
  const unsigned hand_index = static_cast<unsigned>(WeaponHandIndex(cfg.dominant_hand_left));
  const HandSample& hand = frame.hand[hand_index];
  bool tracking_valid = fresh_frame && hand.aim_valid;
  Vec3 direction{};
  if (tracking_valid) {
    float norm2 = 0.0f;
    for (float component : hand.aim_quat) norm2 += component * component;
    if (!std::isfinite(norm2) || norm2 <= 1e-6f) {
      tracking_valid = false;
    } else {
      const float inv_norm = 1.0f / std::sqrt(norm2);
      direction = QuatRotate({0, 0, -1}, hand.aim_quat[0] * inv_norm,
                             hand.aim_quat[1] * inv_norm,
                             hand.aim_quat[2] * inv_norm,
                             hand.aim_quat[3] * inv_norm);
    }
  }
  if (theater_screen) {
    float fov = g_theater_pointer_fov_rad.load(std::memory_order_relaxed);
    if (!std::isfinite(fov) || fov <= 0.0f)
      fov = cfg.mono_screen_fov_deg * 0.0174532925f;
    auto pointer = ProjectTheaterPointer(
        true, tracking_valid, hand.trigger_held,
        {frame.head_pos[0], frame.head_pos[1], frame.head_pos[2]},
        frame.head_quat, {hand.aim_pos[0], hand.aim_pos[1], hand.aim_pos[2]},
        direction, cfg.menu_distance_m, fov, kTheaterAspect,
        display_width, display_height);
    MapSurfacePointerToContent(pointer, display_width, display_height,
                               kTheaterAspect);
    pointer.navigation_select = fresh_frame && hand.primary_held;
    return pointer;
  }
  const bool title = g_menu_debounce.Current().state == MenuState::FrontEnd;
  const MenuAnchor& anchor = calibration_surface
      ? g_calibration_anchor : g_menu_anchor;
  const bool anchored = calibration_surface
      ? g_calibration_anchored : g_menu_anchored;
  float panel_width = calibration_surface
      ? g_calibration_panel_width : g_panel_width.load(std::memory_order_relaxed);
  if (!(panel_width > 0.0f))
    panel_width = MenuWidth(cfg.menu_distance_m,
        title ? 60.0f : cfg.menu_reading_fov_deg);
  auto pointer = ProjectMenuPointer(
      menu_surface, anchored, tracking_valid, hand.trigger_held,
      {frame.head_pos[0], frame.head_pos[1], frame.head_pos[2]},
      {hand.aim_pos[0], hand.aim_pos[1], hand.aim_pos[2]}, direction,
      anchor, panel_width,
      kTheaterAspect, display_width, display_height);
  MapSurfacePointerToContent(pointer, display_width, display_height,
                             kTheaterAspect);
  pointer.navigation_select = fresh_frame && hand.primary_held;
  return pointer;
}

void XrRecordCameraSequence(std::uint64_t sequence) {
  std::lock_guard<std::mutex> lock(g_camera_tag_mutex);
  g_camera_sequence = sequence;
  g_camera_thread = sequence ? GetCurrentThreadId() : 0;
  g_camera_frame = {}; // a diagnostic sequence alone cannot certify a source pose
}
void XrRecordCameraFrame(const TrackingFrame& frame,float rendered_vfov_rad) {
  std::lock_guard<std::mutex> lock(g_camera_tag_mutex);
  g_camera_sequence = frame.sequence;
  g_camera_thread = frame.sequence ? GetCurrentThreadId() : 0;
  g_camera_frame = frame;
  g_camera_frame.vfov_rad = rendered_vfov_rad;
}
std::uint64_t XrAppliedCameraSequence() {
  std::lock_guard<std::mutex> lock(g_camera_tag_mutex);
  return g_camera_sequence;
}

bool XrEffectiveRate(EffectiveRate& out) {
  std::lock_guard<std::mutex> lock(g_rate_mutex);
  if (g_rate_snapshot.generation != 0) { out = g_rate_snapshot; return true; }
  out = ResolveEffectiveRate(0, 0);
  return true;
}

double XrLastDisplayRefreshHz() { return g_display_refresh_hz.load(); }

bool XrRenderMetricsSnapshot(RenderMetrics& out) {
  std::lock_guard<std::mutex> lock(g_metrics_mutex);
  out = g_metrics_snapshot;
  return out.frame_sequence != 0;
}

bool XrViewFovTotals(float& vfov_rad, float& aspect) {
  TrackingFrame frame{};
  if (!XrLatestTrackingFrame(frame) || !std::isfinite(frame.vfov_rad) ||
      !std::isfinite(frame.fov_aspect) || frame.vfov_rad <= 0.5f ||
      frame.fov_aspect <= 0.1f) return false;
  vfov_rad = frame.vfov_rad;
  aspect = frame.fov_aspect;
  return true;
}

}  // namespace dsvr
