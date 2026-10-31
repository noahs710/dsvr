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
XrSwapchain g_swapchain[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
std::vector<XrSwapchainImageD3D11KHR> g_images[2];
uint32_t g_eye_width = 0;
uint32_t g_eye_height = 0;
int64_t g_eye_format = 0;
unsigned long long g_submitted = 0;
uint32_t g_rec_width = 0;
uint32_t g_rec_height = 0;
uint32_t g_max_eye_width = 0, g_max_eye_height = 0;
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
ID3D11Buffer* g_blit_aa_constants = nullptr;
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
bool g_gameplay_observed=false;
std::atomic<bool> g_sanctuary_override{false};
std::atomic<bool> g_recenter_menu{false};
bool g_menu_anchored = false;
MenuAnchor g_menu_anchor{};
// Presentation state is debounced (menu_state.h): raw per-frame readings
// (title heuristic / manual override) must hold 150 ms before the canvas
// opens or closes, so menu navigation flicker never flashes immersive
// mode. Render thread only; no lock needed.
MenuDebouncer g_menu_debounce{150};
ID3D11Texture2D* g_srv_source = nullptr;
ID3D11ShaderResourceView* g_blit_srv = nullptr;
ID3D11Texture2D* g_stage = nullptr;  // our copy of the backbuffer, SRV-bindable
uint32_t g_stage_w = 0;
uint32_t g_stage_h = 0;
DXGI_FORMAT g_stage_fmt = DXGI_FORMAT_UNKNOWN;
std::vector<ID3D11RenderTargetView*> g_rtvs[2];
bool g_format_logged = false;
bool g_theater_parity_logged = false;
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
  // The natural camera/player observation remains independent of presentation:
  // the calibration screen deliberately uses theater while awaiting this pose.
  const bool trigger = (frame.hand[0].valid && frame.hand[0].trigger_held) ||
                       (frame.hand[1].valid && frame.hand[1].trigger_held);
  bool floor_valid = false;
  float floor_height = 0;
  if (g_floor_space && CalibrationGetStatus().screen_visible) {
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
  CalibrationSample(frame, trigger, CameraGameplayReady(), floor_valid, floor_height,
                     profile.valid ? profile.eye_height_m : Config{}.eye_height);
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
  if(previous.faulted)g_pump_failed.store(true);
  if(g_pump_failed.load()) {
    if(!g_pump_fault_logged) {
      g_pump_fault_logged=true;
      DSVR_ERROR("XR: inactive frame cycle failed wait=%d begin=%d end=%d; VR suspended until session restart",
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
  return g_running.load() && !g_pump_failed.load() &&
      (g_state==XR_SESSION_STATE_VISIBLE || g_state==XR_SESSION_STATE_FOCUSED) &&
      g_inactive_pump.PauseAndIdle() && !g_inactive_pump.Snapshot().faulted;
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
  if (g_swapchain[0] || g_swapchain[1]) {
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
    if (g_swapchain[i]) {
      if (!Ok(xrDestroySwapchain(g_swapchain[i]), "xrDestroySwapchain")) {
        destroyed = false;
        continue;  // Keep this owner and its images for a later cleanup attempt.
      }
      g_native_frame.SwapchainDestroyed(g_swapchain[i]);
    }
    g_mono_images[i] = {};
    for (auto* rtv : g_rtvs[i])
      if (rtv) rtv->Release();
    g_rtvs[i].clear();
    g_swapchain[i] = XR_NULL_HANDLE;
    g_images[i].clear();
  }
  if (!destroyed) return false;
  if (g_destroy_query) g_destroy_query->Release();
  g_destroy_query = nullptr;
  g_destroy_pending = false;
  g_eye_width = g_eye_height = 0;
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
        g_blit_aa_constants) return true;
  // A failed creation retries cleanly rather than leaking the partial pipeline.
  const auto release = [](auto*& p) { if (p) p->Release(); p = nullptr; };
  release(g_blit_vs); release(g_blit_ps); release(g_blit_sampler);
  release(g_blit_blend); release(g_blit_depth); release(g_blit_raster);
  release(g_blit_constants); release(g_blit_aa_constants);
  // FXAA 3.11-style luma edge pass folded into the blit: single-tap
    // fast path when disabled (aa_on == 0), 5-tap + 4 edge taps when on.
    // MSAA resolve happens in hardware on the RTV; this pass only smooths
    // the sampled image. Runs on every blit; cost is one fullscreen pass.
    static const char* kShader =
      "Texture2D tex : register(t0); SamplerState samp : register(s0);"
      "cbuffer Comfort : register(b0) { float4 tangents; float4 vignette; };"
      "cbuffer Aa : register(b1) { float4 aa; };"
      "struct PSIn { float4 pos : SV_Position; float2 uv : TEXCOORD0; };"
      "PSIn VSMain(uint id : SV_VertexID) { PSIn o;"
      "  o.uv = float2((id << 1) & 2, id & 2);"
      "  o.pos = float4(o.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);"
      "  return o; }"
      "float Luma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }"
      "float4 PSMain(PSIn i) : SV_Target {"
      "  float4 c = tex.Sample(samp, i.uv);"
      "  if (aa.x > 0.5) {"
      "    float2 px = aa.yz;"
      "    float l0 = Luma(c.rgb);"
      "    float lx = Luma(tex.Sample(samp, i.uv + float2(px.x, 0)).rgb);"
      "    float ly = Luma(tex.Sample(samp, i.uv + float2(0, px.y)).rgb);"
      "    float lxn = Luma(tex.Sample(samp, i.uv - float2(px.x, 0)).rgb);"
      "    float lyn = Luma(tex.Sample(samp, i.uv - float2(0, px.y)).rgb);"
      "    float range = max(max(lx, ly), max(lxn, lyn)) - min(min(lx, ly), min(lxn, lyn));"
      "    if (range > 0.0312) {"
      "      float2 dir = float2(lx - lxn, ly - lyn);"
      "      float len = max(length(dir), 1e-4);"
      "      dir /= len;"
      "      float2 off = dir * px * 0.5;"
      "      float3 avg = (tex.Sample(samp, i.uv + off).rgb +"
      "                  tex.Sample(samp, i.uv - off).rgb) * 0.5;"
      "      float la = Luma(avg);"
      "      float blend = smoothstep(0.0312, 0.125, range) * 0.75;"
      "      blend *= 1.0 - abs(la - l0) / max(range, 1e-4);"
      "      c.rgb = lerp(c.rgb, avg, saturate(blend));"
      "    }"
      "  }"
      "  float2 uv = i.pos.xy * vignette.yz;"
      "  float2 t = float2(lerp(tangents.x,tangents.y,uv.x),"
      "                    lerp(tangents.z,tangents.w,uv.y));"
      "  float inner = lerp(1.4,0.55,vignette.x);"
      "  c.rgb *= 1.0-vignette.x*smoothstep(inner,inner+0.35,length(t));"
      "  return c; }";
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
    g_device->CreateBuffer(&aa_constants, nullptr, &g_blit_aa_constants);
    return g_blit_vs && g_blit_ps && g_blit_sampler && g_blit_blend &&
         g_blit_depth && g_blit_raster && g_blit_constants &&
         g_blit_aa_constants;
}

bool PrepareMonoSource(ID3D11Texture2D* backbuffer,
                       const D3D11_TEXTURE2D_DESC& src_desc) {
  // The game's backbuffer is render-target-only (no SRV bind flag), so it
  // cannot be sampled directly. Copy it (same format+size, always legal)
  // into our own SRV-bindable texture and sample that instead.
  if (!g_stage || !g_blit_srv || g_stage_w != src_desc.Width ||
      g_stage_h != src_desc.Height || g_stage_fmt != src_desc.Format) {
    if (g_stage) g_stage->Release();
    if (g_blit_srv) g_blit_srv->Release();
    g_stage = nullptr;
    g_blit_srv = nullptr;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = src_desc.Width;
    td.Height = src_desc.Height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = src_desc.Format;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(g_device->CreateTexture2D(&td, nullptr, &g_stage))) {
      DSVR_ERROR("XR: staging texture creation failed");
      return false;
    }
    g_stage_w = src_desc.Width;
    g_stage_h = src_desc.Height;
    g_stage_fmt = src_desc.Format;
    HRESULT hr =
        g_device->CreateShaderResourceView(g_stage, nullptr, &g_blit_srv);
    if (FAILED(hr) || !g_blit_srv) {
      DSVR_ERROR("XR: staging SRV creation failed hr=0x%08lx", (unsigned long)hr);
      return false;
    }
    DSVR_LOG("XR: staging texture ready %ux%u format=%d", g_stage_w, g_stage_h,
             (int)g_stage_fmt);
  }
  g_context->CopyResource(g_stage, backbuffer);
  g_srv_source = backbuffer;
  return g_blit_srv != nullptr;
}

bool BlitToEye(int eye, uint32_t index, const D3D11_TEXTURE2D_DESC& src_desc,
               const XrFovf& fov, float vignette) {
  if (!g_blit_srv || index >= g_rtvs[eye].size()) return false;
  ID3D11RenderTargetView* rtv = g_rtvs[eye][index];
  if (!rtv) {
    DSVR_ERROR("XR: blit skipped, null RTV eye=%d index=%u", eye, index);
    return false;
  }
  // Aspect-preserving fit of the game image into this eye's virtual-screen
  // rect (full eye rect until the first located view sets it).
  float rx = g_screen_rect[eye][2] > 0 ? g_screen_rect[eye][0] : 0.0f;
  float ry = g_screen_rect[eye][2] > 0 ? g_screen_rect[eye][1] : 0.0f;
  float rw = g_screen_rect[eye][2] > 0 ? g_screen_rect[eye][2]
                                       : (float)g_eye_width;
  float rh = g_screen_rect[eye][3] > 0 ? g_screen_rect[eye][3]
                                       : (float)g_eye_height;
  // Immersive mode stretches to fill: the game projection is driven to
  // the eye aspect, so its 16:9 backbuffer holds a squeezed eye-aspect
  // image that must be unsqueezed across the full eye rect. Theater mode
  // aspect-fits inside the virtual-screen rect instead.
  D3D11_VIEWPORT vp{};
  if (g_immersive) {
    vp = D3D11_VIEWPORT{rx, ry, rw, rh, 0.0f, 1.0f};
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
  const bool full_eye = rx == 0.0f && ry == 0.0f &&
      rw >= (float)g_eye_width && rh >= (float)g_eye_height;
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
      1.f / g_eye_width, 1.f / g_eye_height, 0};
  g_context->UpdateSubresource(g_blit_constants, 0, nullptr, constants, 0, 0);
  g_context->PSSetConstantBuffers(0, 1, &g_blit_constants);
  // FXAA constants: on/off + source texel size. MSAA resolve is hardware.
  {
    const int aa = DiagnosticsCurrentConfig().aa_mode;
    const float aa_on = (aa == 1) ? 1.0f : 0.0f;
    const float aa_c[4]{aa_on,
        src_desc.Width ? 1.0f / (float)src_desc.Width : 0.0f,
        src_desc.Height ? 1.0f / (float)src_desc.Height : 0.0f, 0.0f};
    g_context->UpdateSubresource(g_blit_aa_constants, 0, nullptr, aa_c, 0, 0);
    g_context->PSSetConstantBuffers(1, 1, &g_blit_aa_constants);
  }
  g_context->IASetInputLayout(nullptr);
  g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  g_context->Draw(3, 0);
  ID3D11ShaderResourceView* none = nullptr;
  g_context->PSSetShaderResources(0, 1, &none);
  g_context->OMSetRenderTargets(0, nullptr, nullptr);
  return true;
}


bool EnsureSwapchains(ID3D11Texture2D* backbuffer) {
  D3D11_TEXTURE2D_DESC desc{};
  backbuffer->GetDesc(&desc);
  // One eye size for the whole session: the virtual-screen rect is computed
  // from g_eye_width/height, so rebuilding the swapchains mid-session leaves
  // that rect pointing at extents that no longer exist. Sizing comes from the
  // runtime's recommended rect, which is what the compositor costs fairly.
  if (g_swapchain[0] && g_swapchain[1] && !g_images[0].empty() &&
      !g_images[1].empty() && g_head_space && !g_destroy_pending) return true;
  if (!DestroySwapchains()) return false;
  bool complete = false;
  struct SetupGuard {
    bool& complete;
    ~SetupGuard() { if (!complete) DestroySwapchains(); }
  } setup_guard{complete};
  if (!EnsureBlit()) return false;
  if (!g_eye_scale_percent)
    g_eye_scale_percent = std::clamp(DiagnosticsCurrentConfig().render_scale_percent, 50, 200);
  const auto chosen_size = ChooseEyeBufferSize(g_rec_width, g_rec_height, g_max_eye_width,
                                               g_max_eye_height, desc.Width, desc.Height,
                                               g_eye_scale_percent);
  const uint32_t eye_width = chosen_size.width;
  const uint32_t eye_height = chosen_size.height;

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
      DSVR_WARN("XR: LOCAL space unavailable; sanctuary uses projection fallback");
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

  // AA: MSAA sample count from config (FXAA/off = 1 sample + shader pass).
    // Unsupported counts fall back to 1; the FXAA pass still runs when asked.
    const int aa_mode = DiagnosticsCurrentConfig().aa_mode;
    uint32_t want_samples = (aa_mode == 3) ? 4 : (aa_mode == 2) ? 2 : 1;
    for (int eye = 0; eye < 2; ++eye) {
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
    if (!Ok(xrCreateSwapchain(g_session, &info, &g_swapchain[eye]),
            "xrCreateSwapchain"))
      return false;
    uint32_t count = 0;
    if (!Ok(xrEnumerateSwapchainImages(g_swapchain[eye], 0, &count, nullptr),
            "xrEnumerateSwapchainImages(count)") || !count) return false;
    g_images[eye].assign(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    const uint32_t capacity = count;
    if (!Ok(xrEnumerateSwapchainImages(g_swapchain[eye], capacity, &count,
               (XrSwapchainImageBaseHeader*)g_images[eye].data()),
            "xrEnumerateSwapchainImages") || !count || count > capacity) return false;
    g_images[eye].resize(count);
    g_rtvs[eye].assign(count, nullptr);
    for (uint32_t i = 0; i < count; ++i) {
      if (!g_images[eye][i].texture) return false;
      // SteamVR swapchain textures are TYPELESS: a default RTV inherits the
      // typeless format and fails with E_INVALIDARG (found live). An
      // explicit RTV format (the swapchain format) is required. MSAA
      // swapchains need the MSAA RTV dimension; a failed MSAA RTV falls
      // back to a single-sample swapchain for this eye.
      D3D11_RENDER_TARGET_VIEW_DESC rtv_desc{};
      rtv_desc.Format = (DXGI_FORMAT)chosen;
      if (info.sampleCount > 1) {
          rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
        } else {
          rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
          rtv_desc.Texture2D.MipSlice = 0;
        }
        HRESULT hr = g_device->CreateRenderTargetView(
          g_images[eye][i].texture, &rtv_desc, &g_rtvs[eye][i]);
      if (FAILED(hr) || !g_rtvs[eye][i]) {
        D3D11_TEXTURE2D_DESC td{};
        g_images[eye][i].texture->GetDesc(&td);
        DSVR_ERROR("XR: eye%d swapchain image%u RTV failed hr=0x%08lx tex=%p "
                   "texformat=%d bind=0x%x",
                   eye, i, (unsigned long)hr,
                   (void*)g_images[eye][i].texture, (int)td.Format,
                    td.BindFlags);
        return false;
      }
    }
    DSVR_LOG("XR: eye%d RTVs created first=%p", eye, (void*)g_rtvs[eye][0]);
  }
  g_eye_width = eye_width;
  g_eye_height = eye_height;
  g_eye_format = chosen;
  complete = true;
  DSVR_LOG("XR: eye swapchains ready %ux%u format=%d images=%u/%u scale=%d%% "
           "(runtime recommended %ux%u max %ux%u; restart to change scale)",
           g_eye_width, g_eye_height, (int)chosen,
           (unsigned)g_images[0].size(), (unsigned)g_images[1].size(),
           g_eye_scale_percent,
           g_rec_width, g_rec_height, g_max_eye_width, g_max_eye_height);
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
      view_count >= 2) {
    std::vector<XrViewConfigurationView> views(
        view_count, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    const auto view_capacity = view_count;
    const auto view_result = xrEnumerateViewConfigurationViews(
        g_instance, g_system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
        view_count, &view_count, views.data());
    if (XR_FAILED(view_result) || view_count < 2 || view_count > view_capacity) {
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
    g_rec_width = views[0].recommendedImageRectWidth;
    g_rec_height = views[0].recommendedImageRectHeight;
    g_max_eye_width = views[0].maxImageRectWidth;
    g_max_eye_height = views[0].maxImageRectHeight;
    for (uint32_t i = 1; i < view_count; ++i) {
      if (views[i].recommendedImageRectWidth > g_rec_width)
        g_rec_width = views[i].recommendedImageRectWidth;
      if (views[i].recommendedImageRectHeight > g_rec_height)
        g_rec_height = views[i].recommendedImageRectHeight;
      if (views[i].maxImageRectWidth &&
          (!g_max_eye_width || views[i].maxImageRectWidth < g_max_eye_width))
        g_max_eye_width = views[i].maxImageRectWidth;
      if (views[i].maxImageRectHeight &&
          (!g_max_eye_height || views[i].maxImageRectHeight < g_max_eye_height))
        g_max_eye_height = views[i].maxImageRectHeight;
    }
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
  if (!GraphicsFrameReady() || g_state != XR_SESSION_STATE_FOCUSED) {
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
  if (!g_native_frame.Acquire(g_swapchain[0], g_swapchain[1])) {
    g_native_frame.Cancel();
    InvalidateTracking();
    return false;
  }
  PublishRate(g_native_frame.State().predictedDisplayPeriod);
  NativeStereoFrame prediction{};
  prediction.token = g_native_frame.Token();
  prediction.predicted_time_ns = g_native_frame.State().predictedDisplayTime;
  prediction.width = g_eye_width;
  prediction.height = g_eye_height;
  prediction.color_format = g_eye_format;
  TrackingFrame tracking{};
  tracking.focused = tracking.orientation_valid = tracking.position_valid = true;
  tracking.predicted_time_ns = prediction.predicted_time_ns;
  tracking.display_period_ns = g_native_frame.State().predictedDisplayPeriod;
  for (unsigned eye = 0; eye < 2; ++eye) {
    const auto index = g_native_frame.ImageIndex(eye);
    if (index >= g_images[eye].size() || !g_images[eye][index].texture) {
      g_native_frame.Cancel(); InvalidateTracking(); return false;
    }
    auto& target = prediction.eyes[eye];
    target.texture2d = g_images[eye][index].texture;
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
      g_state != XR_SESSION_STATE_FOCUSED ||
      GetTickCount64() - g_native_begin_ms > 100 ||
      !g_tracking_frames.Read(GetTickCount64(), 100, tracking) ||
      tracking.sequence != g_native_prediction.tracking_sequence ||
      tracking.space_epoch != g_native_prediction.space_epoch) {
    g_native_frame.Cancel(); InvalidateTracking(); return false;
  }
  const bool submitted = g_native_frame.Submit(g_native_prediction.width,
                                               g_native_prediction.height);
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
  auto* backbuffer = (ID3D11Texture2D*)backbuffer_texture2d;
  std::uint64_t camera_sequence_at_entry = 0;
  DWORD camera_thread_at_entry = 0;
  TrackingFrame camera_frame_at_entry;
  {
    std::lock_guard<std::mutex> lock(g_camera_tag_mutex);
    camera_sequence_at_entry = g_camera_sequence;
    camera_thread_at_entry = g_camera_thread;
    camera_frame_at_entry = g_camera_frame;
  }

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
  XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
  XrCompositionLayerProjectionView views_out[2]{};
  const XrCompositionLayerBaseHeader* layers[1] = {
      (const XrCompositionLayerBaseHeader*)&layer};
  bool render = false;
  bool published = false;
  bool tracking_failed = false;
  std::uint64_t published_sequence = 0;
  std::uint64_t published_epoch = 0;

  if (state.shouldRender && EnsureSwapchains(backbuffer)) {
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
      if (!head_valid ||
          !(view_state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {
        g_menu_anchored = false;
      }
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
      render = head_valid &&
          (view_state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0;
      Config presentation_cfg = DiagnosticsCurrentConfig();
      if(CameraGameplayReady())g_gameplay_observed=true;
      // Raw reading: manual override (config or F8) outranks the title
      // heuristic. Only the debounced snapshot may change presentation;
      // the native all-menu detector is still pending (H2), so in-game
      // menus remain manual (F8) -- do not claim otherwise. Loading (no
      // gameplay sample yet) and non-3D cutscenes force theater: they are
        // never immersive, and the stick must read as menu navigation there.
        // Gameplay itself is immersive by default (headlook_enabled=true);
        // [vr] gameplay_theater=true is the manual flat-screen escape.
        const bool loading = !g_gameplay_observed;
        const bool cutscene = CameraNativeCutscene();
        // Two questions that used to be one boolean: does a panel need the
          // pointer/stick route, and should the game image be replaced by a
          // head-anchored canvas. The calibration screen is drawn INTO the game
          // image, so it routes input but never takes the canvas -- taking it
          // filled the view up close and hid the loading image behind it
          // (live report 31 Oct 2026).
          const MenuState raw_menu =
              presentation_cfg.sanctuary_override ||
                    g_sanctuary_override.load() || ModMenuVisible()
                ? MenuState::ReadingMenu
                : CalibrationGetStatus().screen_visible
                      ? MenuState::CalibrationPanel
                : (loading || cutscene || CameraIsTitle())
                      ? MenuState::FrontEnd
                      : MenuState::Gameplay;
      const unsigned long long gen_before =
          g_menu_debounce.Current().generation;
      const MenuSnapshot snap =
          g_menu_debounce.Feed(raw_menu, (long long)GetTickCount64());
      const bool title = snap.state == MenuState::FrontEnd;
      const bool menu = MenuStateWantsPanel(snap.state);
      const bool spatial_menu =
            MenuStateWantsSanctuary(snap.state) && g_local_space;
        if (snap.generation != gen_before) {
        g_menu_anchored = false;
          DSVR_LOG("XR: panel %s (debounced state=%d gen=%llu); "
                 "in-game automatic detection pending",
                 menu ? "open" : "closed", (int)snap.state,
                   snap.generation);
      }
      g_menu_active.store(menu);
      if (g_recenter_menu.exchange(false)) g_menu_anchored = false;
      if (!menu) g_menu_anchored = false;
      {
        Config cfg = DiagnosticsCurrentConfig();
        // Publish FOV totals for the game's (single, symmetric)
        // projection: take the eye with the widest vertical FOV so
        // neither eye is asked to show more than the game rendered.
        // Pure runtime data -- correct on any HMD by construction.
        {
          int widest = 0;
          float widest_v = 0.0f;
          for (int eye = 0; eye < 2; ++eye) {
            float v = views[eye].fov.angleUp - views[eye].fov.angleDown;
            if (v > widest_v) {
              widest_v = v;
              widest = eye;
            }
          }
          float tl = std::tan(views[widest].fov.angleLeft);
          float tr = std::tan(views[widest].fov.angleRight);
          float tu = std::tan(views[widest].fov.angleUp);
          float td = std::tan(views[widest].fov.angleDown);
          g_vfov_rad = widest_v;
          g_fov_aspect = (tr - tl) / (tu - td);
          if (!g_fov_logged) {
            g_fov_logged = true;
            DSVR_LOG("XR: game frustum from eye%d: vfov=%.1fdeg aspect=%.3f "
                     "(runtime-derived, no headset constants)",
                     widest, g_vfov_rad * 57.29578f, g_fov_aspect);
          }
        }
        g_immersive = cfg.headlook_enabled && !cfg.gameplay_theater && !menu;
        if (g_immersive) {
          // Immersive mode: draw the FP image at the tangent-correct
          // position in each eye. Filling the whole eye buffer per eye
          // (previous build) put image centre ~15 deg apart between eyes
          // because the two frustums are mirror-asymmetric -- the eyes
          // could not fuse it ("2 overlapping images"). The game renders
          // a symmetric frustum (tangents +/-tanH, +/-tanV from the FOV
          // we drive), so map exactly those tangents into each eye.
          const float immersive_vfov = camera_frame_at_entry.sequence
              ? camera_frame_at_entry.vfov_rad
              : g_vfov_rad * std::clamp(cfg.fov_scale, 0.7f, 1.3f);
          float tan_v = std::tan(immersive_vfov * 0.5f);
          float tan_h_game = tan_v * (camera_frame_at_entry.sequence
              ? camera_frame_at_entry.fov_aspect : g_fov_aspect);
          const SymmetricFrustum game{tan_h_game, tan_v, immersive_vfov,
                camera_frame_at_entry.sequence ? camera_frame_at_entry.fov_aspect
                                               : g_fov_aspect};
            for (int eye = 0; eye < 2; ++eye) {
            // One tested implementation for both the mono immersive path and
            // the H3 hook (stereo_math.h SubRectForEye). Filling the whole eye
            // buffer per eye put the image centre ~15 deg apart between eyes
            // because the two frustums are mirror-asymmetric -- the eyes could
            // not fuse it ("2 overlapping images"). Vertical is tangent-mapped
            // too: a pixel-linear full-height form stretched the image
            // increasingly toward the edges, a visible warp on pitch.
            const EyeFrustum frustum{std::tan(views[eye].fov.angleLeft),
                                       std::tan(views[eye].fov.angleRight),
                                       std::tan(views[eye].fov.angleUp),
                                       std::tan(views[eye].fov.angleDown)};
            const EyeSubRect r = SubRectForEye(game, frustum);
            g_screen_rect[eye][0] = r.x * g_eye_width;
              g_screen_rect[eye][1] = r.y * g_eye_height;
            g_screen_rect[eye][2] = r.w * g_eye_width;
            g_screen_rect[eye][3] = r.h * g_eye_height;
          }
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
              FrustumFromAngles(views[0].fov.angleLeft, views[0].fov.angleRight,
                                views[0].fov.angleUp, views[0].fov.angleDown),
              FrustumFromAngles(views[1].fov.angleLeft, views[1].fov.angleRight,
                                views[1].fov.angleUp, views[1].fov.angleDown)};
            // The panel IS the game image, so its aspect is the source image's
            // own. Deriving it from the eye frustum instead (aspect 0.925 on
          // this runtime) produced a near-square panel with the 2.39:1 game
            // image letterboxed inside it: the image filled ~41% of the panel,
          // so it read far away and blurry while the panel itself was far too
          // wide (live report 31 Oct 2026). Ask the backbuffer directly:
          // g_stage_w/h are only filled by PrepareMonoSource later in this
          // same frame, so on the first frame and after a format change they
          // still read 0.
          D3D11_TEXTURE2D_DESC theater_source{};
          backbuffer->GetDesc(&theater_source);
          const float source_aspect =
                theater_source.Height
                    ? (float)theater_source.Width / (float)theater_source.Height
                    : g_fov_aspect;
            const auto requested_screen = TheaterScreenExtents(
              theater_fov_deg * 0.0174532925f, source_aspect);
            const auto eye_fitted = FitTheaterScreenToEyes(
              requested_screen, theater_eyes, source_aspect);
            // Pixel parity: never magnify the game image past its own source
            // resolution, or the panel reads soft and jagged.
            const auto theater_screen = FitTheaterScreenToPixels(
              eye_fitted, theater_eyes, g_eye_width, g_eye_height,
              theater_source.Width, theater_source.Height);
          if (!g_theater_parity_logged &&
              theater_screen.tan_half_h + 1e-6f < eye_fitted.tan_half_h) {
            g_theater_parity_logged = true;
            DSVR_LOG("XR: theater screen shrunk to source parity "
                     "(%.1fdeg -> %.1fdeg; source %ux%u, eye %ux%u)",
                     2.0f * std::atan(eye_fitted.tan_half_h) * 57.29578f,
                     2.0f * std::atan(theater_screen.tan_half_h) * 57.29578f,
                     theater_source.Width, theater_source.Height,
                     g_eye_width, g_eye_height);
          }
          g_theater_pointer_fov_rad.store(2.0f * std::atan(theater_screen.tan_half_h), std::memory_order_relaxed);
          for (int eye = 0; eye < 2; ++eye) {
              const PixelRect rect = TheaterRectForEye(
                  theater_screen, theater_eyes[eye], (float)g_eye_width,
                  (float)g_eye_height);
              g_screen_rect[eye][0] = rect.x;
              g_screen_rect[eye][1] = rect.y;
              g_screen_rect[eye][2] = rect.w;
              g_screen_rect[eye][3] = rect.h;
          }
        }  // immersive vs theater rect
        if (spatial_menu) {
          // Reuse the left swapchain, with its aspect-fit content cropped
          // exactly by the quad subimage. Both eyes see the same texture.
          g_screen_rect[0][0] = g_screen_rect[0][1] = 0;
          g_screen_rect[0][2] = (float)g_eye_width;
          g_screen_rect[0][3] = (float)g_eye_height;
        }
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
        if (frame.focused && frame.orientation_valid && frame.position_valid) {
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
        if (!source.sequence || source.sequence != camera_sequence_at_entry ||
            source.space_epoch != g_tracking_frames.CurrentEpoch() ||
            !source.focused || !source.orientation_valid || !source.position_valid ||
            now < source.captured_ms || now - source.captured_ms > 100 ||
            source.predicted_time_ns <= 0 || source.predicted_time_ns > state.predictedDisplayTime ||
            !std::isfinite(source.vfov_rad) || source.vfov_rad <= .5f || source.vfov_rad >= 3.1f ||
            !std::isfinite(source.fov_aspect) || source.fov_aspect <= 0)
          render = false;
        else metrics.source_tracking_sequence = source.sequence;
      }
      // Snapshot the source once, before either eye can consume it. Both eye
      // draws sample this same frame, including format conversion/aspect fit.
      D3D11_TEXTURE2D_DESC src_desc{};
      backbuffer->GetDesc(&src_desc);
      metrics.source_width = src_desc.Width;
      metrics.source_height = src_desc.Height;
      metrics.eye_width = g_eye_width;
      metrics.eye_height = g_eye_height;
      metrics.theater = menu;
      const float head_quat[4]{head.orientation.x, head.orientation.y,
                               head.orientation.z, head.orientation.w};
      const float vignette = g_comfort_vignette.Update(InputLatestComfortMotion(),
          head_quat, state.predictedDisplayTime, g_tracking_frames.CurrentEpoch(),
          render && published && g_immersive && presentation_cfg.comfort_vignette,
          presentation_cfg.vignette_strength);
      if (render) {
        g_gpu_timer.BeginFrame(g_device, g_context, metrics.frame_sequence);
        g_gpu_timer.BeginRange(g_context, 0);
        const auto copy_start = QpcNow();
        render = PrepareMonoSource(backbuffer, src_desc);
        metrics.source_copy_cpu_ms = QpcElapsedMs(QpcNow() - copy_start);
        metrics.source_copies = render ? 1 : 0;
        g_gpu_timer.EndRange(g_context, 0);
      }
      for (int eye = 0; eye < (spatial_menu ? 1 : 2) && render; ++eye) {
        uint32_t index = 0;
        XrSwapchainImageAcquireInfo acquire{
            XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        const auto acquired = xrAcquireSwapchainImage(g_swapchain[eye], &acquire, &index);
        if (XR_SUCCEEDED(acquired)) g_mono_images[eye].chain = g_swapchain[eye];
        if (acquired != XR_SUCCESS) {
          Ok(acquired, "xrAcquireSwapchainImage");
          render = false;
          break;
        }
        XrSwapchainImageWaitInfo wait_image{
            XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wait_image.timeout = 5'000'000;
        const auto image_wait_start = QpcNow();
        const auto waited = xrWaitSwapchainImage(g_swapchain[eye], &wait_image);
        metrics.image_wait_ms += QpcElapsedMs(QpcNow() - image_wait_start);
        g_mono_images[eye].ready = XR_SUCCEEDED(waited) && waited != XR_TIMEOUT_EXPIRED;
        if (waited != XR_SUCCESS) {
          if(waited==XR_TIMEOUT_EXPIRED)++g_mono_image_timeouts;
          Ok(waited, "xrWaitSwapchainImage");
          render = false;
          break;
        }
        g_gpu_timer.BeginRange(g_context, 1 + eye);
        const auto blit_start = QpcNow();
        render = BlitToEye(eye, index, src_desc, views[eye].fov, vignette);
        metrics.eye_blit_cpu_ms += QpcElapsedMs(QpcNow() - blit_start);
        if (render) ++metrics.eye_draws;
        g_gpu_timer.EndRange(g_context, 1 + eye);
        XrSwapchainImageReleaseInfo release{
            XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        const auto released = xrReleaseSwapchainImage(g_swapchain[eye], &release);
        if (XR_SUCCEEDED(released)) g_mono_images[eye] = {};
        if (released != XR_SUCCESS) {
          Ok(released, "xrReleaseSwapchainImage");
          render = false;
          break;
        }

        views_out[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
        views_out[eye].pose = views[eye].pose;
        if (g_immersive) {
          // Mono pixels originate at the center camera, including on canted
          // displays: neither eye's newer orientation/IPD rendered this image.
          const auto& source = camera_frame_at_entry;
          views_out[eye].pose.orientation = {source.head_quat[0],source.head_quat[1],
                                            source.head_quat[2],source.head_quat[3]};
          views_out[eye].pose.position = {source.head_pos[0],source.head_pos[1],source.head_pos[2]};
        }
        views_out[eye].fov = views[eye].fov;
        views_out[eye].subImage.swapchain = g_swapchain[eye];
        views_out[eye].subImage.imageRect.offset = {0, 0};
        views_out[eye].subImage.imageRect.extent = {(int32_t)g_eye_width,
                                                    (int32_t)g_eye_height};
        views_out[eye].subImage.imageArrayIndex = 0;
      }
      g_gpu_timer.EndFrame(g_context);
      if (render) {
        layer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
        layer.space = g_space;
        layer.viewCount = 2;
        layer.views = views_out;
        if (spatial_menu) {
          if (!(view_state.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT)) {
            // Hide until translation tracking returns: a local-space anchor
            // needs a valid position. Re-anchor on recovery.
            render = false;
            g_menu_anchored = false;
          } else {
            if (!g_menu_anchored) {
              const auto& q = head.orientation;
              const float fx = -2.0f * (q.x * q.z + q.w * q.y);
              const float fz = -(1.0f - 2.0f * (q.x * q.x + q.y * q.y));
              const float yaw = std::atan2(-fx, -fz);
              const float x = head.position.x;
              const float y = head.position.y;
              const float z = head.position.z;
              g_menu_anchor = MakeMenuAnchor(x, y, z, yaw, presentation_cfg.menu_distance_m);
              g_menu_anchored = true;
            }
            const float scale = std::min((float)g_eye_width / src_desc.Width,
                                           (float)g_eye_height / src_desc.Height);
            const int32_t w = std::max(1, (int32_t)(src_desc.Width * scale));
              const int32_t h = std::max(1, (int32_t)(src_desc.Height * scale));
            quad.space = g_space;
            quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            quad.subImage.swapchain = g_swapchain[0];
            quad.subImage.imageRect.offset = {((int32_t)g_eye_width - w) / 2,
                                              ((int32_t)g_eye_height - h) / 2};
            quad.subImage.imageRect.extent = {w, h};
            quad.pose.position = {g_menu_anchor.x, g_menu_anchor.y, g_menu_anchor.z};
            quad.pose.orientation.y = std::sin(g_menu_anchor.yaw * 0.5f);
            quad.pose.orientation.w = std::cos(g_menu_anchor.yaw * 0.5f);
            quad.size.width = MenuWidth(presentation_cfg.menu_distance_m,
                                        title ? 60.0f : presentation_cfg.menu_reading_fov_deg);
            quad.size.height = quad.size.width * src_desc.Height / src_desc.Width;
            layers[0] = (const XrCompositionLayerBaseHeader*)&quad;
          }
        }
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
  end.layerCount = render ? 1 : 0;
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
  const bool submitted_quad = end.layerCount && layers[0] ==
      reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad);
  trace.flags = (ended ? 1U : 0U) | (render ? 2U : 0U) | (submitted_quad ? 4U : 0U);
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
    if (g_submitted % 600 == 0) {
      ID3D11Texture2D* colour = nullptr;
      ID3D11Texture2D* depth = nullptr;
      std::uint32_t w = 0, h = 0;
      std::uint64_t draws = 0;
      if (DepthProbeMainPair(colour, depth, w, h, draws))
        DSVR_LOG("DepthProbe: main pair colour=%p %ux%u depth=%p draws=%llu "
                 "(identification only; depth not yet consumed)",
                 (void*)colour, w, h, (void*)depth,
                 (unsigned long long)draws);
    }
  }
}

bool XrSessionRunning() { return g_running.load(); }
bool XrMenuPresentationActive() {
  // Every signal here is hard: no running session, a session the runtime is
  // not showing, a faulted frame pump, or the debounced front-end reading.
  // VISIBLE and FOCUSED both count as visible (PollEvents), so a focus blip
  // cannot flip this on its own; MenuDebouncer already carries the hysteresis
  // on the menu reading, and a missing camera sample must force theater at
  // once (loading and non-3D cutscenes are never immersive).
  return !g_running.load() || !g_session_visible.load() || g_pump_failed.load() || g_menu_active.load();
}
void XrToggleSanctuary() {
  bool old = g_sanctuary_override.load();
  g_sanctuary_override.store(!old);
}
void XrRecenterSanctuary() { g_recenter_menu.store(true); }

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
  const bool menu_surface = running && g_menu_active.load() && g_space && g_local_space;
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
  const float aspect = display_height
      ? static_cast<float>(display_width) / display_height : 0.0f;
  if (theater_screen) {
    float fov = g_theater_pointer_fov_rad.load(std::memory_order_relaxed);
    if (!std::isfinite(fov) || fov <= 0.0f)
      fov = cfg.mono_screen_fov_deg * 0.0174532925f;
    auto pointer = ProjectTheaterPointer(
        true, tracking_valid, hand.trigger_held,
        {frame.head_pos[0], frame.head_pos[1], frame.head_pos[2]},
        frame.head_quat, {hand.aim_pos[0], hand.aim_pos[1], hand.aim_pos[2]},
        direction, cfg.menu_distance_m, fov, aspect, display_width, display_height);
    pointer.navigation_select = fresh_frame && hand.primary_held;
    return pointer;
  }
  const bool title = g_menu_debounce.Current().state == MenuState::FrontEnd;
  auto pointer = ProjectMenuPointer(
      menu_surface, g_menu_anchored, tracking_valid, hand.trigger_held,
      {frame.head_pos[0], frame.head_pos[1], frame.head_pos[2]},
      {hand.aim_pos[0], hand.aim_pos[1], hand.aim_pos[2]}, direction,
      g_menu_anchor, MenuWidth(cfg.menu_distance_m,
          title ? 60.0f : cfg.menu_reading_fov_deg),
      aspect, display_width, display_height);
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
