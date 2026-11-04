// Normal DXGI render-host dispatch plus a dedicated, UI-only observer-menu hook.
// The observer-menu detours never enter the XR/render-host dispatch path.
//
// Normal DXGI host path (fresh implementation, standard DXGI practice):
//  1. Create a throwaway D3D11 device + swapchain on a hidden window.
//  2. Read Present (vtable slot 8) and ResizeBuffers (slot 13) addresses.
//  3. MinHook those addresses; every game's swapchain shares the vtable.
//  4. Detours log once (desc + adapter), count frames, forward to original.
//
// ReShade mode uses the versioned bridge below instead of these DXGI hooks.
// Other core subsystems have their own executable/config gates.

#include "render.h"
#include "render_host.h"
#include "iat_patch.h"
#include "reshade_bridge.h"

#include <windows.h>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <MinHook.h>

#include <atomic>

#include "../common/logging.h"
#include "camera.h"
#include "calibration.h"
#include "core.h"
#include "depth_probe.h"
#include "afr_policy.h"
#include "diagnostics.h"
#include "stereo_gate.h"
#include "xr.h"
#include "vr_trace.h"
#include "mod_menu.h"
#include "cpu_frame_timer.h"

namespace dsvr {
namespace {

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT,
                                                    UINT, UINT, DXGI_FORMAT,
                                                    UINT);

PresentFn g_present_orig = nullptr;
ResizeBuffersFn g_resize_orig = nullptr;
void* g_present_target = nullptr;
void* g_resize_target = nullptr;

using GetForegroundWindowFn = HWND(WINAPI*)();
using ClipCursorFn = BOOL(WINAPI*)(const RECT*);
using SetCaptureFn = HWND(WINAPI*)(HWND);
iat::IatPatch g_fg_iat;
iat::IatPatch g_active_iat;
iat::IatPatch g_focuswnd_iat;
iat::IatPatch g_visible_iat;
iat::IatPatch g_clip_iat;
iat::IatPatch g_capture_iat;
std::atomic<std::uintptr_t> g_focus_spoof_hwnd{0};
void UpdateFocusSpoof(HWND hwnd, const Config& cfg);
std::atomic<bool> g_focus_spoof_active{false};

std::atomic<unsigned long long> g_frames{0};
std::atomic<bool> g_described{false};
bool g_verbose = false;
long long g_qpf = 1;
long long g_last_fps_tick = 0;
unsigned long long g_last_fps_frame = 0;
std::atomic_flag g_fps_busy = ATOMIC_FLAG_INIT;
// Desktop-only readiness never configures or publishes an XR render host.
std::atomic<bool> g_desktop_ready{false};
std::atomic<bool> g_desktop_notified{false};
std::atomic<bool> g_observer_menu_only{false};
RenderHost g_host;
ReShadeBridge g_reshade_bridge{g_host};

struct ProbeInternalPass {
  ProbeInternalPass() { DepthProbeBeginInternalPass(); }
  ~ProbeInternalPass() { DepthProbeEndInternalPass(); }
  ProbeInternalPass(const ProbeInternalPass&) = delete;
  ProbeInternalPass& operator=(const ProbeInternalPass&) = delete;
};

long long NowQpc() {
  LARGE_INTEGER v{};
  QueryPerformanceCounter(&v);
  return v.QuadPart;
}

void RecordHostRateEvidence() {
  g_frames.fetch_add(1, std::memory_order_relaxed);
  if (!g_fps_busy.test_and_set(std::memory_order_acquire)) {
    long long now = NowQpc();
    if (g_last_fps_tick == 0) {
      g_last_fps_tick = now;
      g_last_fps_frame = g_frames.load();
    } else if (now - g_last_fps_tick >= g_qpf * 5) {  // every ~5 s
      unsigned long long f = g_frames.load();
      double secs = (double)(now - g_last_fps_tick) / (double)g_qpf;
      const double game_fps = (double)(f - g_last_fps_frame) / secs;
      // Rate evidence on every run (not verbose-only): measured game FPS
      // beside the effective target and runtime rate, per README policy.
      EffectiveRate rate{};
      XrEffectiveRate(rate);
      DSVR_LOG("RateEvidence: game_fps=%.1f observed_app=%d runtime=%.3fHz "
               "headset=%.3fHz source=%s (frames total %llu)",
               game_fps, rate.effective_fps, rate.runtime_hz, rate.display_hz,
               rate.from_runtime ? "runtime" : "no-runtime-sample", f);
      RenderMetrics metrics{};
      if (XrRenderMetricsSnapshot(metrics))
        DSVR_LOG("VRPerformance: mode=mono frame=%llu cpu=%.3fms "
                 "xr_wait=%.3fms image_wait=%.3fms copy_cpu=%.3fms "
                 "blit_cpu=%.3fms end_cpu=%.3fms copies=%u eyes=%u "
                 "gpu_valid=%d gpu_frame=%llu gpu_copy=%.3fms gpu_blit=%.3fms",
                 (unsigned long long)metrics.frame_sequence, metrics.cpu_frame_ms,
                 metrics.xr_wait_ms, metrics.image_wait_ms, metrics.source_copy_cpu_ms,
                 metrics.eye_blit_cpu_ms, metrics.end_frame_cpu_ms, metrics.source_copies,
                 metrics.eye_draws, (int)metrics.gpu_valid,
                 (unsigned long long)metrics.gpu_frame_sequence, metrics.gpu_copy_ms,
                 metrics.gpu_blit_ms);
      if(metrics.frame_sequence)
        DSVR_LOG("VRImage: source=%ux%u generation=%llu retained=%d age_ms=%llu eyes=%ux%u/%ux%u",
            metrics.source_width,metrics.source_height,
            static_cast<unsigned long long>(metrics.source_generation),static_cast<int>(metrics.source_retained),
            static_cast<unsigned long long>(metrics.source_age_ms),
            metrics.eye_widths[0],metrics.eye_heights[0],metrics.eye_widths[1],metrics.eye_heights[1]);
      if(metrics.frame_sequence)
        DSVR_LOG("VRSubmissionHealth: frame=%llu submitted=%d pose_published=%d image_timeouts=%llu held_image_skips=%llu",
            static_cast<unsigned long long>(metrics.frame_sequence),static_cast<int>(metrics.submitted),
            static_cast<int>(metrics.tracking_published),static_cast<unsigned long long>(metrics.image_timeouts),
            static_cast<unsigned long long>(metrics.held_image_skips));
      if (g_verbose)
        DSVR_LOG("Render: %.1f fps over last interval (%llu frames total)",
                 game_fps, f);
      const auto cpu=g_cpu_frame_timer.TakeInterval();
      for(unsigned i=0;i<cpu.size();++i)if(cpu[i].calls)
        DSVR_LOG("CpuPerformance: stage=%s calls=%llu avg=%.3fms max=%.3fms (inclusive, interval=%.3fs; not summable)",
            kCpuStageNames[i],static_cast<unsigned long long>(cpu[i].calls),
            cpu[i].AverageMs(),cpu[i].max_ms,secs);
      g_last_fps_tick = now;
      g_last_fps_frame = f;
    }
    g_fps_busy.clear(std::memory_order_release);
  }
}

void CaptureHostDevice(void*, std::uintptr_t device) {
  CoreOnRenderFrame();
  auto* native = reinterpret_cast<ID3D11Device*>(device);
  // Exactly one process-lifetime reference: XR has no device teardown/migration
  // contract yet. Temporary frame device and image references are balanced.
  native->AddRef();
  DSVR_LOG("Render: sole host captured D3D11 device=%p (held for XR lifetime)",
           static_cast<void*>(native));
  XrOnDeviceCaptured(native);
}

void PollHost(void*) {
  CoreOnRenderFrame();
  XrOnPresentFrame();
}

void SubmitHostMono(void*, const HostSource& source, HostStamp stamp) {
  CpuStageScope host_time(CpuStage::HostFrame);
  DepthProbeFrameBoundary(reinterpret_cast<ID3D11Texture2D*>(source.image),
                          stamp.ordinal);
  const auto capture = VrTraceCaptureId();
  if (capture && VrTraceActive()) {
    VrTraceEvent trace{};
    trace.capture_id = capture;  // all three observations belong to one capture
    trace.event = "host_source";
    trace.object = source.image;
    trace.frame = stamp.ordinal;
    trace.flags = source.host == RenderHostKind::ReShade ? 2u : 1u;
    // Lossless local RESOURCE generation limbs. Never set epoch/predicted_ns:
    // this observation knows neither native scene nor consumed-view identity.
    for (unsigned i = 0; i < 4; ++i)
      trace.values[i] = static_cast<float>((stamp.generation >> (16 * i)) & 0xffff);
    D3D11_TEXTURE2D_DESC desc{};
    reinterpret_cast<ID3D11Texture2D*>(source.image)->GetDesc(&desc);
    trace.values[4] = static_cast<float>(desc.Width);
    trace.values[5] = static_cast<float>(desc.Height);
    trace.values[6] = static_cast<float>(desc.Format);
    trace.values[7] = static_cast<float>(desc.SampleDesc.Count);
    VrTraceRecord(trace);
    trace.event = "host_swapchain";
    trace.object = source.swapchain;
    VrTraceRecord(trace);
    trace.event = "host_device";
    trace.object = source.device;
    VrTraceRecord(trace);
  }
  // Desktop menus render directly. While the headset is visible, the XR owner
  // renders the menu to its independent transparent surface instead.
  const Config cfg = DiagnosticsCurrentConfig();
  CalibrationPresentationReady(cfg.vr_enabled && !cfg.observer_only && XrSessionVisible());
  if (!XrSessionVisible()) {
    ProbeInternalPass probe_scope;
    ModMenuRenderTexture(reinterpret_cast<ID3D11Device*>(source.device),
                         reinterpret_cast<ID3D11Texture2D*>(source.image));
  }
  if (XrSessionRunning()) {
    // T3 stereo gate: no native eye renderer is bound (Present sees one
    // finished image), so a stereo request refuses once and mono continues.
    // XrBeginNativeStereoFrame runs only on AttemptStereo, which requires
    // that renderer; duplicating this image would be fake stereo.
    constexpr bool kNativeEyeRendererBound = false;
    static std::atomic<bool> stereo_refusal_logged{false};
    if (DecideStereo(cfg.stereo_enabled, true, kNativeEyeRendererBound) ==
            StereoDecision::RefuseNoRenderer &&
        !stereo_refusal_logged.exchange(true))
      DSVR_WARN("Stereo: stereo_enabled requested but no native eye renderer "
                "is bound; continuing mono (no duplicated-eye substitute)");
    static std::atomic<bool> afr_refusal_logged{false};
    if (DecideAfr(cfg.afr_enabled, false) == AfrDecision::RefuseStereoInactive &&
        !afr_refusal_logged.exchange(true))
      DSVR_WARN("AFR: afr_enabled requested while stereo is inactive; "
                "alternating a mono image proves nothing, continuing mono");
    ProbeInternalPass probe_scope;
    XrSubmitMonoFrame(reinterpret_cast<void*>(source.image));
  }
  RecordHostRateEvidence();
}

void DispatchTexture(RenderHostKind host, std::uintptr_t swapchain,
                     ID3D11Texture2D* image) {
  ID3D11Device* device = nullptr;
  image->GetDevice(&device);
  if (!device) return;
  g_host.Dispatch({host, reinterpret_cast<std::uintptr_t>(device), swapchain,
                   reinterpret_cast<std::uintptr_t>(image), GetCurrentThreadId()});
  device->Release();
}

void DescribeOnce(IDXGISwapChain* swap) {
  bool expected = false;
  if (!g_described.compare_exchange_strong(expected, true)) return;
  DXGI_SWAP_CHAIN_DESC desc{};
  if (SUCCEEDED(swap->GetDesc(&desc))) {
    DSVR_LOG("Render: swapchain %ux%u format=%d buffers=%u windowed=%d "
             "sample=%u sync-via-present",
             desc.BufferDesc.Width, desc.BufferDesc.Height,
             (int)desc.BufferDesc.Format, desc.BufferCount,
             desc.Windowed ? 1 : 0, desc.SampleDesc.Count);
  } else {
    DSVR_LOG("Render: swapchain GetDesc failed");
  }
  ID3D11Device* device = nullptr;
  if (SUCCEEDED(swap->GetDevice(__uuidof(ID3D11Device), (void**)&device)) &&
      device) {
    IDXGIDevice* dxgi_device = nullptr;
    if (SUCCEEDED(device->QueryInterface(__uuidof(IDXGIDevice),
                                         (void**)&dxgi_device)) &&
        dxgi_device) {
      IDXGIAdapter* adapter = nullptr;
      if (SUCCEEDED(dxgi_device->GetAdapter(&adapter)) && adapter) {
        DXGI_ADAPTER_DESC ad{};
        if (SUCCEEDED(adapter->GetDesc(&ad))) {
          char name[256]{};
          WideCharToMultiByte(CP_UTF8, 0, ad.Description, -1, name,
                              sizeof(name) - 1, nullptr, nullptr);
          DSVR_LOG("Render: device adapter '%s' vram=%llu MB feature-level "
                   "device=%p",
                   name,
                   (unsigned long long)(ad.DedicatedVideoMemory / 1048576),
                   (void*)device);
        }
        adapter->Release();
      }
      dxgi_device->Release();
    }
    device->Release();  // describing a swapchain never claims XR ownership
  }
}

// Render only the existing menu into this process's backbuffer. Closed menus do
// not inspect the swapchain or acquire any D3D/DXGI references.
void RenderObserverMenu(IDXGISwapChain* swap) {
  if (!ModMenuVisible() || XrSessionVisible()) return;
  DXGI_SWAP_CHAIN_DESC desc{};
  if (FAILED(swap->GetDesc(&desc)) ||
      !RenderWindowBelongsToProcess(desc.OutputWindow))
    return;
  ID3D11Texture2D* backbuffer = nullptr;
  const HRESULT buffer_result = swap->GetBuffer(
      0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backbuffer));
  if (FAILED(buffer_result) || !backbuffer) {
    if (backbuffer) backbuffer->Release();
    return;
  }
  ID3D11Device* device = nullptr;
  backbuffer->GetDevice(&device);
  if (device) {
    ModMenuRenderTexture(device, backbuffer);
    device->Release();
  }
  backbuffer->Release();
}

std::atomic<bool> g_sync_logged{false};
// Mirror-off black-out: the headset copy already ran in DispatchTexture, so
// clearing here only affects the desktop backbuffer. A skipped Present would
// otherwise freeze the last image on screen; black proves "no mirror".
void ClearBackbufferForNoMirror(IDXGISwapChain* swap) {
  ID3D11Texture2D* backbuffer = nullptr;
  if (FAILED(swap->GetBuffer(0, __uuidof(ID3D11Texture2D),
                             reinterpret_cast<void**>(&backbuffer))) ||
      !backbuffer)
    return;
  ID3D11Device* device = nullptr;
  backbuffer->GetDevice(&device);
  if (device) {
    ID3D11DeviceContext* context = nullptr;
    device->GetImmediateContext(&context);
    if (context) {
      ID3D11RenderTargetView* rtv = nullptr;
      if (SUCCEEDED(device->CreateRenderTargetView(backbuffer, nullptr, &rtv)) &&
          rtv) {
        const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        context->ClearRenderTargetView(rtv, black);
        rtv->Release();
      }
      context->Release();
    }
    device->Release();
  }
  backbuffer->Release();
}

// No-mirror window set. The game process can own several visible top-level
// windows at once (the game window plus the simulator status window), and
// the game can recreate its window at any time. While the mirror is off,
// every such window is minimized (before the session shows) or hidden (once
// it shows); every tracked window is restored when the mirror returns, the
// session hides, or hooks unload. Only the render thread calls these, so a
// plain fixed-size set is sufficient.
struct NoMirrorWin {
  HWND hwnd = nullptr;
  bool hidden = false;
  bool minimized = false;
};
NoMirrorWin g_no_mirror_wins[4];

NoMirrorWin* TrackNoMirrorWin(HWND hwnd) {
  for (auto& slot : g_no_mirror_wins)
    if (slot.hwnd == hwnd) return &slot;
  for (auto& slot : g_no_mirror_wins) {
    if (!slot.hwnd || !IsWindow(slot.hwnd)) {
      slot.hwnd = hwnd;
      slot.hidden = false;
      slot.minimized = false;
      return &slot;
    }
  }
  return nullptr;
}

bool IsNoMirrorCandidate(HWND hwnd) {
  if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd)) return false;
  if (GetWindow(hwnd, GW_OWNER)) return false;
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid != GetCurrentProcessId()) return false;
  char cls[64] = {};
  GetClassNameA(hwnd, cls, static_cast<int>(sizeof(cls) - 1));
  return strcmp(cls, "ConsoleWindowClass") != 0;
}

struct NoMirrorScan {
  HWND hwnds[4];
  int count = 0;
};

BOOL CALLBACK CollectNoMirrorWindows(HWND hwnd, LPARAM out) {
  auto* scan = reinterpret_cast<NoMirrorScan*>(out);
  if (scan->count >= 4) return FALSE;
  if (!IsNoMirrorCandidate(hwnd)) return TRUE;
  scan->hwnds[scan->count++] = hwnd;
  return TRUE;
}

void HideNoMirrorCandidate(HWND hwnd) {
  if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd)) return;
  NoMirrorWin* slot = TrackNoMirrorWin(hwnd);
  if (!slot || slot->hidden) return;
  slot->hidden = true;
  slot->minimized = false;
  ShowWindowAsync(hwnd, SW_HIDE);
  DSVR_LOG("Render: no-mirror game window hidden (hwnd=%p)",
           static_cast<void*>(hwnd));
}

void MinimizeNoMirrorCandidate(HWND hwnd) {
  if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd)) return;
  NoMirrorWin* slot = TrackNoMirrorWin(hwnd);
  if (!slot || slot->hidden || slot->minimized) return;
  slot->minimized = true;
  // A window the user already minimized still takes the flag so a later
  // restore brings it back instead of merely showing it.
  if (!IsIconic(hwnd)) ShowWindowAsync(hwnd, SW_SHOWMINNOACTIVE);
  DSVR_LOG("Render: no-mirror game window minimized while XR starts (hwnd=%p)",
           static_cast<void*>(hwnd));
}

// Re-hide tracked windows the game re-showed behind our back; each re-show
// is logged, since it means something actively fights the hidden state.
void ReassertNoMirrorHidden() {
  for (auto& slot : g_no_mirror_wins) {
    if (!slot.hidden || !slot.hwnd || !IsWindow(slot.hwnd)) continue;
    if (!IsWindowVisible(slot.hwnd)) continue;
    ShowWindowAsync(slot.hwnd, SW_HIDE);
    DSVR_LOG("Render: no-mirror game window re-hidden (hwnd=%p)",
             static_cast<void*>(slot.hwnd));
  }
}

void RestoreNoMirrorWindows() {
  for (auto& slot : g_no_mirror_wins) {
    if ((!slot.hidden && !slot.minimized) || !slot.hwnd) continue;
    HWND hwnd = slot.hwnd;
    slot.hwnd = nullptr;
    slot.hidden = false;
    slot.minimized = false;
    if (!IsWindow(hwnd)) continue;
    // Restore (not show) so a minimized window leaves the taskbar.
    ShowWindowAsync(hwnd, SW_RESTORE);
    DSVR_LOG("Render: no-mirror game window restored (hwnd=%p)",
             static_cast<void*>(hwnd));
  }
}


HRESULT STDMETHODCALLTYPE PresentDetour(IDXGISwapChain* swap,
                                        UINT sync_interval, UINT flags) {
  if (g_observer_menu_only.load(std::memory_order_acquire)) {
    if (!(flags & DXGI_PRESENT_TEST)) RenderObserverMenu(swap);
    return g_present_orig(swap, sync_interval, flags);
  }
  if (g_desktop_ready.load(std::memory_order_acquire)) {
    // Observe a real, successful desktop Present only. No trace/menu/XR calls,
    // host selection, device capture, or retained COM reference on this path.
    const HRESULT result = g_present_orig(swap, sync_interval, flags);
    DXGI_SWAP_CHAIN_DESC desc{};
    if (!g_desktop_notified.load(std::memory_order_acquire) &&
        SUCCEEDED(result) && !(flags & DXGI_PRESENT_TEST) &&
        SUCCEEDED(swap->GetDesc(&desc)) && RenderWindowBelongsToProcess(desc.OutputWindow)) {
      ID3D11Texture2D* buffer = nullptr;
      if (SUCCEEDED(swap->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                    reinterpret_cast<void**>(&buffer))) && buffer) {
        D3D11_TEXTURE2D_DESC texture{};
        buffer->GetDesc(&texture);
        buffer->Release();
        if (texture.Width && texture.Height &&
            !g_desktop_notified.exchange(true, std::memory_order_acq_rel)) CoreOnDesktopFrame();
      }
    }
    return result;
  }
  if (!g_sync_logged.exchange(true)) {
    // A zero sync interval rules out desktop vsync for THIS call only.
    // Native, driver, runtime and GPU waits require separate measurement.
    DSVR_LOG("Render: Present sync_interval=%u flags=0x%x (frame-cap "
             "evidence; see MODLOG)",
             sync_interval, flags);
  }
  VrTraceEvent trace{};
  trace.event = "present_enter";
  trace.object = reinterpret_cast<std::uintptr_t>(swap);
  trace.flags = flags;
  trace.values[0] = static_cast<float>(sync_interval);
  VrTraceRecord(trace);
  DescribeOnce(swap);
  DXGI_SWAP_CHAIN_DESC desc{};
  if (!(flags & DXGI_PRESENT_TEST) && SUCCEEDED(swap->GetDesc(&desc)) &&
      RenderWindowBelongsToProcess(desc.OutputWindow)) {
    ID3D11Texture2D* backbuffer = nullptr;
    if (SUCCEEDED(swap->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                  (void**)&backbuffer)) &&
        backbuffer) {
      DispatchTexture(RenderHostKind::Present,
                      reinterpret_cast<std::uintptr_t>(swap), backbuffer);
      backbuffer->Release();
    }
  }
  const long long start = VrTraceClock();
  CpuStageScope present_time(CpuStage::NativePresent);
  // Desktop Present is pure cost in VR: the backbuffer was already copied
  // to the eye images above, and the native limiter (not Present) paces
  // the game. Skipping it removes desktop composition and any vsync wait.
  // TEST presents and non-VR configs always forward to the original.
  const Config present_cfg = DiagnosticsCurrentConfig();
  const bool skip_desktop = !(flags & DXGI_PRESENT_TEST) &&
      present_cfg.desktop_present == false &&
      present_cfg.vr_enabled;
  if (skip_desktop) {
    // Mirror off means no desktop image at all: black the backbuffer, then
    // minimize (before XR shows) or hide (once it shows) every window this
    // process owns. The XR copy above already ran, so the headset image is
    // unaffected.
    ClearBackbufferForNoMirror(swap);
    NoMirrorScan scan{};
    if (desc.OutputWindow && IsWindow(desc.OutputWindow) &&
        RenderWindowBelongsToProcess(desc.OutputWindow))
      scan.hwnds[scan.count++] = desc.OutputWindow;
    // Recreated windows surface about once a second; tracked slots cover
    // the steady state without scanning every present.
    static int presents_since_scan = 0;
    if (++presents_since_scan >= 60) {
      presents_since_scan = 0;
      EnumWindows(CollectNoMirrorWindows, reinterpret_cast<LPARAM>(&scan));
    }
    for (int i = 1; i < scan.count; ++i) {
      if (scan.hwnds[i] == scan.hwnds[0]) scan.hwnds[i] = scan.hwnds[--scan.count];
    }
    UpdateFocusSpoof(scan.count ? scan.hwnds[0] : nullptr, present_cfg);
    if (ShouldHideDesktopWindow(present_cfg.desktop_present,
                                present_cfg.vr_enabled, XrSessionVisible())) {
      for (int i = 0; i < scan.count; ++i) HideNoMirrorCandidate(scan.hwnds[i]);
      ReassertNoMirrorHidden();
    } else if (ShouldMinimizeDesktopWindow(present_cfg.desktop_present,
                                           present_cfg.vr_enabled,
                                           XrSessionVisible())) {
      for (int i = 0; i < scan.count; ++i)
        MinimizeNoMirrorCandidate(scan.hwnds[i]);
    }
  } else {
    UpdateFocusSpoof(nullptr, present_cfg);
    RestoreNoMirrorWindows();
  }

  const HRESULT result = skip_desktop
      ? S_OK
      : g_present_orig(swap, sync_interval, flags);
  present_time.Stop();
  trace.event = "present_exit";
  trace.duration_qpc = VrTraceClock() - start;
  trace.flags = static_cast<unsigned>(result);
  VrTraceRecord(trace);
  return result;
}

HRESULT STDMETHODCALLTYPE ResizeDetour(IDXGISwapChain* swap, UINT buffers,
                                       UINT width, UINT height,
                                       DXGI_FORMAT format, UINT flags) {
  if (g_observer_menu_only.load(std::memory_order_acquire))
    return g_resize_orig(swap, buffers, width, height, format, flags);
  DSVR_LOG("Render: ResizeBuffers %ux%u format=%d buffers=%u", width, height,
           (int)format, buffers);
  g_described.store(false);  // re-describe after a resize
  g_host.Invalidate(RenderHostKind::Present,
                    reinterpret_cast<std::uintptr_t>(swap));
  const HRESULT result = g_resize_orig(swap, buffers, width, height, format, flags);
  // A resize that returns S_OK but leaves the old size means the game renders
  // with a viewport that no longer matches its backbuffer, which presents as a
  // black window. The HRESULT plus the re-described size is the only evidence
  // that separates "the game refused to resize" from "our copy used a stale size".
  DXGI_SWAP_CHAIN_DESC after{};
  const bool described = SUCCEEDED(swap->GetDesc(&after));
  DSVR_LOG("Render: ResizeBuffers result=0x%8.8lX afterwards=%ux%u buffers=%u",
           (unsigned long)result, described ? after.BufferDesc.Width : 0,
           described ? after.BufferDesc.Height : 0,
           described ? after.BufferCount : 0);
  return result;
}

// The hidden no-mirror window never owns the real foreground, so games that
// gate pad input on focus would go deaf in the headset. Spoof only foreign
// queries: our own UI, hotkeys and diagnostics keep seeing the real answer,
// which matters because hotkeys must not fire while the user types elsewhere.
// From the player's perspective the hidden game is always the active,
// focused, visible application. These three queries complete the foreground
// lie for menus and dialogs that check a different one than the title
// screen; same spoof-active gate, same game-module-only scope.
HWND WINAPI GetActiveWindowDetour() {
  const auto orig = reinterpret_cast<GetForegroundWindowFn>(g_active_iat.original);
  if (!g_focus_spoof_active.load(std::memory_order_acquire)) return orig();
  const auto spoof = reinterpret_cast<HWND>(
      g_focus_spoof_hwnd.load(std::memory_order_acquire));
  if (spoof && IsWindow(spoof)) return spoof;
  return orig();
}

HWND WINAPI GetFocusDetour() {
  const auto orig = reinterpret_cast<GetForegroundWindowFn>(g_focuswnd_iat.original);
  if (!g_focus_spoof_active.load(std::memory_order_acquire)) return orig();
  const auto spoof = reinterpret_cast<HWND>(
      g_focus_spoof_hwnd.load(std::memory_order_acquire));
  if (spoof && IsWindow(spoof)) return spoof;
  return orig();
}

BOOL WINAPI IsWindowVisibleDetour(HWND hwnd) {
  const auto orig = reinterpret_cast<BOOL(WINAPI*)(HWND)>(g_visible_iat.original);
  if (!hwnd || !IsWindow(hwnd)) return orig(hwnd);
  if (!g_focus_spoof_active.load(std::memory_order_acquire)) return orig(hwnd);
  // The tracked game windows render every frame into the headset; report
  // them visible so visibility-gated UI keeps ticking. Our own module calls
  // the real export (its IAT is untouched), so re-show detection is honest.
  for (const auto& slot : g_no_mirror_wins) {
    if (slot.hwnd == hwnd && (slot.hidden || slot.minimized)) return TRUE;
  }
  return orig(hwnd);
}

HWND WINAPI GetForegroundWindowDetour() {
  const auto orig = reinterpret_cast<GetForegroundWindowFn>(g_fg_iat.original);
  if (!g_focus_spoof_active.load(std::memory_order_acquire)) return orig();
  const auto spoof = reinterpret_cast<HWND>(
      g_focus_spoof_hwnd.load(std::memory_order_acquire));
  if (spoof && IsWindow(spoof)) return spoof;
  return orig();
}

// While the game window is hidden it must not confine or capture the user's
// mouse: a clip rect around an invisible window is exactly the stuck-cursor
// trap. Releases (NULL rect) always pass; confinement attempts are swallowed
// with a success code so the game never retries. In-process only; mirror-on
// passes everything through. Our own code never captures (verified), so no
// caller gate is needed.
BOOL WINAPI ClipCursorDetour(const RECT* rect) {
  const auto orig = reinterpret_cast<ClipCursorFn>(g_clip_iat.original);
  if (!rect) return orig(nullptr);
  if (!g_focus_spoof_active.load(std::memory_order_acquire)) return orig(rect);
  return TRUE;
}

HWND WINAPI SetCaptureDetour(HWND hwnd) {
  if (!g_focus_spoof_active.load(std::memory_order_acquire))
    return reinterpret_cast<SetCaptureFn>(g_capture_iat.original)(hwnd);
  return hwnd;
}

void UpdateFocusSpoof(HWND hwnd, const Config& cfg) {
  const bool active =
      ShouldSpoofFocus(cfg.desktop_present, cfg.vr_enabled);
  if (hwnd && IsWindow(hwnd))
    g_focus_spoof_hwnd.store(reinterpret_cast<std::uintptr_t>(hwnd),
                             std::memory_order_release);
  const bool was = g_focus_spoof_active.exchange(active);
  if (active && !was)
    DSVR_LOG("Render: foreground spoof active (no-mirror input focus)");
}

bool InstallPresentResizeHooks(const char* hook_kind) {
  // This throwaway swapchain supplies only shared DXGI vtable addresses; its
  // device is released here and is never captured or submitted to an XR host.
  HWND hwnd = CreateWindowExW(0, L"STATIC", L"DSVR-dummy", WS_POPUP, 0, 0, 64,
                              64, nullptr, nullptr, GetModuleHandleW(nullptr),
                              nullptr);
  if (!hwnd) {
    DSVR_ERROR("Render: dummy window failed (%lu); no hook", GetLastError());
    return false;
  }
  DXGI_SWAP_CHAIN_DESC desc{};
  desc.BufferCount = 1;
  desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.OutputWindow = hwnd;
  desc.SampleDesc.Count = 1;
  desc.Windowed = TRUE;

  ID3D11Device* device = nullptr;
  ID3D11DeviceContext* context = nullptr;
  IDXGISwapChain* swap = nullptr;
  D3D_FEATURE_LEVEL level{};
  HRESULT hr = D3D11CreateDeviceAndSwapChain(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
      D3D11_SDK_VERSION, &desc, &swap, &device, &level, &context);
  if (FAILED(hr) || !swap) {
    DSVR_ERROR("Render: dummy device failed (hr=0x%08lx); no hook",
               (unsigned long)hr);
    if (swap) swap->Release();
    if (context) context->Release();
    if (device) device->Release();
    DestroyWindow(hwnd);
    return false;
  }
  void** vtable = *(void***)swap;
  g_present_target = vtable[8];
  g_resize_target = vtable[13];
  swap->Release();
  if (context) context->Release();
  if (device) device->Release();
  DestroyWindow(hwnd);

  const MH_STATUS init = MH_Initialize();
  if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
    DSVR_ERROR("Render: MinHook initialize failed; no hook");
    return false;
  }
  if (MH_CreateHook(g_present_target, (void*)&PresentDetour,
                    (void**)&g_present_orig) != MH_OK ||
      MH_CreateHook(g_resize_target, (void*)&ResizeDetour,
                    (void**)&g_resize_orig) != MH_OK) {
    DSVR_ERROR("Render: MinHook create failed; no hook");
    MH_RemoveHook(g_present_target);
    MH_RemoveHook(g_resize_target);
    g_present_target = g_resize_target = nullptr;
    return false;
  }
  if (MH_EnableHook(g_present_target) != MH_OK ||
      MH_EnableHook(g_resize_target) != MH_OK) {
    DSVR_ERROR("Render: MinHook enable failed; no hook");
    MH_DisableHook(g_present_target);
    MH_DisableHook(g_resize_target);
    MH_RemoveHook(g_present_target);
    MH_RemoveHook(g_resize_target);
    g_present_target = g_resize_target = nullptr;
    return false;
  }
  {
    // Game-local IAT patches: deterministic, no trampoline allocation, and
    // scoped to the game module so our own UI, hotkeys and diagnostics keep
    // calling the real exports.
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    const bool fg_ok = !g_fg_iat.slot &&
        InstallIat(user32, "user32.dll", "GetForegroundWindow", g_fg_iat,
                   reinterpret_cast<void*>(&GetForegroundWindowDetour),
                   "FocusSpoof");
    const bool active_ok = !g_active_iat.slot &&
        InstallIat(user32, "user32.dll", "GetActiveWindow", g_active_iat,
                   reinterpret_cast<void*>(&GetActiveWindowDetour),
                   "ActiveSpoof");
    const bool focuswnd_ok = !g_focuswnd_iat.slot &&
        InstallIat(user32, "user32.dll", "GetFocus", g_focuswnd_iat,
                   reinterpret_cast<void*>(&GetFocusDetour), "FocusWndSpoof");
    const bool visible_ok = !g_visible_iat.slot &&
        InstallIat(user32, "user32.dll", "IsWindowVisible", g_visible_iat,
                   reinterpret_cast<void*>(&IsWindowVisibleDetour),
                   "VisibleSpoof");
    const bool clip_ok = !g_clip_iat.slot &&
        InstallIat(user32, "user32.dll", "ClipCursor", g_clip_iat,
                   reinterpret_cast<void*>(&ClipCursorDetour), "ClipGuard");
    const bool capture_ok = !g_capture_iat.slot &&
        InstallIat(user32, "user32.dll", "SetCapture", g_capture_iat,
                   reinterpret_cast<void*>(&SetCaptureDetour), "CaptureGuard");
    DSVR_LOG("Render: no-mirror input guards focus=%d active=%d focuswnd=%d "
             "visible=%d clip=%d capture=%d "
             "(hidden game keeps input, cannot trap the cursor)",
             fg_ok ? 1 : 0, active_ok ? 1 : 0, focuswnd_ok ? 1 : 0,
             visible_ok ? 1 : 0, clip_ok ? 1 : 0, capture_ok ? 1 : 0);
  }
  DSVR_LOG("Render: Present+ResizeBuffers hook installed (%s, level=0x%x)",
           hook_kind, (unsigned)level);
  return true;
}

}  // namespace

bool RenderHookInstall(bool vr_enabled, bool verbose, bool reshade_host) {
  g_observer_menu_only.store(false, std::memory_order_release);
  if (vr_enabled && !g_host.Configure(true,
          reshade_host ? RenderHostKind::ReShade : RenderHostKind::Present,
          {nullptr, CaptureHostDevice, PollHost, SubmitHostMono})) {
    DSVR_LOG("Render: host disabled or already configured; no new owner");
    return false;
  }
  g_desktop_notified.store(false, std::memory_order_release);
  g_desktop_ready.store(!vr_enabled, std::memory_order_release);
  g_verbose = verbose;
  LARGE_INTEGER qpf{};
  QueryPerformanceFrequency(&qpf);
  g_qpf = qpf.QuadPart > 0 ? qpf.QuadPart : 1;
  if (vr_enabled && reshade_host) {
    g_reshade_bridge.SetReady(true);
    DSVR_LOG("Render: ReShade D3D11 host selected; awaiting add-on callbacks; "
             "no DXGI hooks, no automatic fallback (mono source only)");
    return true;
  }

  return InstallPresentResizeHooks(
      vr_enabled ? "mono diagnostic host" : "desktop readiness only");
}

bool ObserverMenuHookInstall() {
  // Publish mode before enabling detours so a racing Present cannot enter the
  // normal XR/render-host dispatch path.
  g_desktop_ready.store(false, std::memory_order_release);
  g_observer_menu_only.store(true, std::memory_order_release);
  if (!InstallPresentResizeHooks("observer-menu UI only")) {
    g_observer_menu_only.store(false, std::memory_order_release);
    return false;
  }
  DSVR_LOG("Render: observer-menu UI hook active; RenderHost unconfigured; "
           "no XR device capture or frame dispatch");
  return true;
}

void RenderHookUninstall() {
  g_desktop_ready.store(false, std::memory_order_release);
  g_observer_menu_only.store(false, std::memory_order_release);
  g_reshade_bridge.SetReady(false);
  RestoreNoMirrorWindows();
  g_focus_spoof_active.store(false, std::memory_order_release);
  RemoveIat(g_fg_iat, reinterpret_cast<void*>(&GetForegroundWindowDetour),
            "FocusSpoof");
  RemoveIat(g_active_iat, reinterpret_cast<void*>(&GetActiveWindowDetour),
            "ActiveSpoof");
  RemoveIat(g_focuswnd_iat, reinterpret_cast<void*>(&GetFocusDetour),
            "FocusWndSpoof");
  RemoveIat(g_visible_iat, reinterpret_cast<void*>(&IsWindowVisibleDetour),
            "VisibleSpoof");
  RemoveIat(g_clip_iat, reinterpret_cast<void*>(&ClipCursorDetour),
            "ClipGuard");
  RemoveIat(g_capture_iat, reinterpret_cast<void*>(&SetCaptureDetour),
            "CaptureGuard");
  if (g_present_target) {
    MH_DisableHook(g_present_target);
    MH_DisableHook(g_resize_target);
    MH_RemoveHook(g_present_target);
    MH_RemoveHook(g_resize_target);
    g_present_target = nullptr;
    g_resize_target = nullptr;
    DSVR_LOG("Render: hooks removed");
  }
}

}  // namespace dsvr

// ABI v1 bridge: called only from the registered ReShade D3D11 callback, never
// DllMain. Export availability does not grant readiness: startup publishes that
// only after the pinned executable gate and selected host configuration.
extern "C" __declspec(dllexport) void __stdcall DsvrReShadeFrameV1(
    std::uintptr_t swapchain, void* resource, void* window) {
  dsvr::g_reshade_bridge.Frame(swapchain, resource, window);
}

extern "C" __declspec(dllexport) void __stdcall DsvrReShadeInvalidateV1(
    std::uintptr_t swapchain) {
  dsvr::g_reshade_bridge.Invalidate(swapchain);
}
