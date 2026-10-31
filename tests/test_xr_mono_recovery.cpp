// Actual mono submission with a fake XR runtime and a headless D3D11 WARP device.
// No headset, game, native stereo rendering or user input is exercised.
#define xrEnumerateInstanceExtensionProperties TestEnumerateInstanceExtensionProperties
#define xrCreateInstance TestCreateInstance
#define xrGetInstanceProperties TestGetInstanceProperties
#define xrGetSystem TestGetSystem
#define xrGetSystemProperties TestGetSystemProperties
#define xrEnumerateViewConfigurationViews TestEnumerateViewConfigurationViews
#define xrGetInstanceProcAddr TestGetInstanceProcAddr
#define xrCreateSession TestCreateSession
#define xrBeginSession TestBeginSession
#define xrPollEvent TestPollEvent
#define xrWaitFrame TestWaitFrame
#define xrBeginFrame TestBeginFrame
#define xrLocateViews TestLocateViews
#define xrLocateSpace TestLocateSpace
#define xrAcquireSwapchainImage TestAcquireSwapchainImage
#define xrWaitSwapchainImage TestWaitSwapchainImage
#define xrReleaseSwapchainImage TestReleaseSwapchainImage
#define xrEndFrame TestEndFrame
#define xrDestroySwapchain TestDestroySwapchain
#define xrDestroySpace TestDestroySpace
#define xrEndSession TestEndSession
#define xrResultToString TestResultToString
#define xrEnumerateSwapchainFormats TestEnumerateSwapchainFormats
#define xrCreateSwapchain TestCreateSwapchain
#define xrEnumerateSwapchainImages TestEnumerateSwapchainImages
#define xrCreateReferenceSpace TestCreateReferenceSpace
#include "../src/core/xr.cpp"

#include <cstdio>
#include <array>

#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)
namespace {
const XrSwapchain chains[2]{reinterpret_cast<XrSwapchain>(11), reinterpret_cast<XrSwapchain>(12)};
bool setup_unavailable=true;unsigned system_calls=0,session_creates=0;
bool advertise_refresh=false,refresh_proc_available=true,refresh_enabled=false;
unsigned refresh_proc_lookups=0,refresh_queries=0;
XrResult refresh_proc_result=XR_SUCCESS,refresh_query_result=XR_SUCCESS;
float physical_refresh=120.0f;
// Fake XR_FB_display_refresh_rate request surface: the listed rates the
// runtime offers, plus the last requested rate and its outcome.
std::vector<float> refresh_offered{};
float refresh_requested = 0.0f;
unsigned refresh_requests = 0;
XrResult refresh_request_result = XR_SUCCESS;
XrDuration application_period=11'111'111;
bool refresh_event_pending=false,events_lost_pending=false,instance_loss_pending=false;
float event_refresh=0.0f;
// Auto-sync observation surface: every live-config rewrite the production
// path applies, in order, plus its outcome.
std::vector<dsvr::Config> requested_configs;
bool request_config_result = true;
XrResult poll_error=XR_SUCCESS;
bool acquired[2]{}, ready[2]{}, frame_begun = false, bad_order = false;
unsigned acquisitions = 0, frame_waits = 0, submitted = 0;
XrDuration longest_wait = 0;
XrResult image_wait[2]{XR_SUCCESS, XR_SUCCESS};
XrResult image_release[2]{XR_SUCCESS, XR_SUCCESS};
XrResult destroy_result[2]{XR_SUCCESS, XR_SUCCESS};
XrTime predicted = 0;
dsvr::Config config{};
dsvr::ComfortMotion comfort_motion{};
dsvr::UserCalibration calibration_profile{};
bool gameplay_ready = true, mod_menu_visible = false, calibration_visible = false;
bool native_cutscene = false;
bool physical_trigger = false;
bool tracking_valid=true,should_render=true;
float head_yaw=0;
XrPosef last_projection_pose{};
XrPosef last_projection_poses[2]{};
XrFovf view_fov[2]{{-1,1,1,-1},{-1,1,1,-1}};
bool block_inactive_wait=false;
HANDLE inactive_wait_entered=nullptr,inactive_wait_release=nullptr;
XrSessionState next_session_state=XR_SESSION_STATE_UNKNOWN;
unsigned ended_sessions=0,begun_sessions=0;
unsigned calibration_samples = 0;
bool sampled_trigger = false, sampled_gameplay = false, sampled_floor = false;
float sampled_height = 0, sampled_fallback = 0;
bool allow_swapchain_creation = false;
unsigned next_created_eye = 0;
uint32_t created_width[2]{}, created_height[2]{}, created_samples[2]{};
ID3D11Texture2D* created_textures[2]{};
int Side(XrSwapchain chain) { return chain == chains[0] ? 0 : 1; }

// Observe the actual WARP context calls made by production xr.cpp. A private
// vtable copy affects only this fixture's context; no game/runtime hooks.
using CopyFn = void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, ID3D11Resource*);
using MapFn = HRESULT (STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT,
                                         D3D11_MAP, UINT, D3D11_MAPPED_SUBRESOURCE*);
using GetDataFn = HRESULT (STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Asynchronous*,
                                              void*, UINT, UINT);
CopyFn original_copy = nullptr;
MapFn original_map = nullptr;
GetDataFn original_get_data = nullptr;
unsigned source_copies = 0, readback_maps = 0;
unsigned query_polls = 0;
bool force_queries_pending = false, query_flush_requested = false;
void STDMETHODCALLTYPE CountCopy(ID3D11DeviceContext* context, ID3D11Resource* dst,
                                  ID3D11Resource* src) {
  ++source_copies;
  original_copy(context, dst, src);
}
HRESULT STDMETHODCALLTYPE CountMap(ID3D11DeviceContext* context, ID3D11Resource* resource,
    UINT subresource, D3D11_MAP kind, UINT flags, D3D11_MAPPED_SUBRESOURCE* out) {
  if (kind == D3D11_MAP_READ) ++readback_maps;
  return original_map(context, resource, subresource, kind, flags, out);
}
HRESULT STDMETHODCALLTYPE CountGetData(ID3D11DeviceContext* context, ID3D11Asynchronous* query,
    void* data, UINT size, UINT flags) {
  ++query_polls;
  query_flush_requested |= !(flags & D3D11_ASYNC_GETDATA_DONOTFLUSH);
  if (force_queries_pending) return S_FALSE;
  return original_get_data(context, query, data, size, flags);
}
}

XRAPI_ATTR XrResult XRAPI_CALL TestEnumerateInstanceExtensionProperties(
    const char*, uint32_t capacity, uint32_t* count, XrExtensionProperties* out) {
  *count=advertise_refresh?2:1;
  if(capacity) {
    if(capacity<*count)return XR_ERROR_SIZE_INSUFFICIENT;
    strcpy_s(out[0].extensionName,XR_KHR_D3D11_ENABLE_EXTENSION_NAME);
    if(advertise_refresh)strcpy_s(out[1].extensionName,XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
  }
  return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL TestCreateInstance(const XrInstanceCreateInfo* info,XrInstance* out) {
  refresh_enabled=false;
  for(uint32_t i=0;i<info->enabledExtensionCount;++i) {
    const auto* name=info->enabledExtensionNames[i];
    if(!strcmp(name,XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME))refresh_enabled=true;
    else if(strcmp(name,XR_KHR_D3D11_ENABLE_EXTENSION_NAME))bad_order=true;
  }
  if(refresh_enabled!=advertise_refresh)bad_order=true;
  *out=reinterpret_cast<XrInstance>(1);return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL TestGetInstanceProperties(XrInstance,XrInstanceProperties* out) {
  strcpy_s(out->runtimeName,"test refresh runtime");return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL TestGetDisplayRefreshRateFB(XrSession,float* out) {
  ++refresh_queries;*out=physical_refresh;return refresh_query_result;
}
XRAPI_ATTR XrResult XRAPI_CALL TestEnumerateDisplayRefreshRatesFB(XrSession,
    uint32_t capacity, uint32_t* count, float* rates) {
  if(count) *count = static_cast<uint32_t>(refresh_offered.size());
  if(capacity && rates) {
    const uint32_t n = capacity < refresh_offered.size() ? capacity : static_cast<uint32_t>(refresh_offered.size());
    for (uint32_t i = 0; i < n; ++i) rates[i] = refresh_offered[i];
  }
  return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL TestRequestDisplayRefreshRateFB(XrSession, float rate) {
  ++refresh_requests;refresh_requested = rate;return refresh_request_result;
}
XRAPI_ATTR XrResult XRAPI_CALL TestGetSystem(XrInstance,const XrSystemGetInfo*,XrSystemId* out) {
  ++system_calls;if(setup_unavailable)return XR_ERROR_FORM_FACTOR_UNAVAILABLE;*out=7;return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL TestGetSystemProperties(XrInstance,XrSystemId,XrSystemProperties*) {return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL TestEnumerateViewConfigurationViews(XrInstance,XrSystemId,XrViewConfigurationType,uint32_t,uint32_t* count,XrViewConfigurationView*) {*count=0;return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL TestGetInstanceProcAddr(XrInstance,const char* name,PFN_xrVoidFunction* out) {
  *out=nullptr;
  if(!strcmp(name,"xrGetDisplayRefreshRateFB")) {
    ++refresh_proc_lookups;
    if(!refresh_enabled)bad_order=true;
    if(refresh_proc_available)*out=reinterpret_cast<PFN_xrVoidFunction>(&TestGetDisplayRefreshRateFB);
    return refresh_proc_result;
  }
  if(!strcmp(name,"xrEnumerateDisplayRefreshRatesFB")) {
    if(!refresh_enabled)bad_order=true;
    if(refresh_proc_available)*out=reinterpret_cast<PFN_xrVoidFunction>(&TestEnumerateDisplayRefreshRatesFB);
    return refresh_proc_result;
  }
  if(!strcmp(name,"xrRequestDisplayRefreshRateFB")) {
    if(!refresh_enabled)bad_order=true;
    if(refresh_proc_available)*out=reinterpret_cast<PFN_xrVoidFunction>(&TestRequestDisplayRefreshRateFB);
    return refresh_proc_result;
  }
  if(strcmp(name,"xrGetD3D11GraphicsRequirementsKHR"))bad_order=true;
  return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL TestCreateSession(XrInstance,const XrSessionCreateInfo*,XrSession* out) {++session_creates;*out=reinterpret_cast<XrSession>(2);return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL TestBeginSession(XrSession,const XrSessionBeginInfo*) {
  ++begun_sessions;return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL TestPollEvent(XrInstance, XrEventDataBuffer* out) {
  if(poll_error!=XR_SUCCESS){const auto result=poll_error;poll_error=XR_SUCCESS;return result;}
  if(refresh_event_pending) {
    auto* event=reinterpret_cast<XrEventDataDisplayRefreshRateChangedFB*>(out);
    *event={XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB};
    event->toDisplayRefreshRate=event_refresh;refresh_event_pending=false;return XR_SUCCESS;
  }
  if(events_lost_pending) {
    *reinterpret_cast<XrEventDataEventsLost*>(out)={XR_TYPE_EVENT_DATA_EVENTS_LOST};
    events_lost_pending=false;return XR_SUCCESS;
  }
  if(instance_loss_pending) {
    *reinterpret_cast<XrEventDataInstanceLossPending*>(out)={XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING};
    instance_loss_pending=false;return XR_SUCCESS;
  }
  if(next_session_state!=XR_SESSION_STATE_UNKNOWN){
    auto* state=reinterpret_cast<XrEventDataSessionStateChanged*>(out);
    *state={XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED};
    state->session=reinterpret_cast<XrSession>(2);state->state=next_session_state;
    next_session_state=XR_SESSION_STATE_UNKNOWN;return XR_SUCCESS;
  }
  return XR_EVENT_UNAVAILABLE;
}
XRAPI_ATTR XrResult XRAPI_CALL TestWaitFrame(XrSession, const XrFrameWaitInfo*, XrFrameState* out) {
  if(block_inactive_wait){
    SetEvent(inactive_wait_entered);
    WaitForSingleObject(inactive_wait_release,2000);
  }
  if (frame_begun) bad_order = true;
  ++frame_waits;
  predicted += application_period;
  if(predicted<1'000'000'000)predicted+=1'000'000'000;
  out->predictedDisplayTime = predicted;
  out->predictedDisplayPeriod = application_period;
  out->shouldRender = should_render?XR_TRUE:XR_FALSE;
  return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL TestBeginFrame(XrSession, const XrFrameBeginInfo*) {
  if (frame_begun) bad_order = true;
  frame_begun = true;
  return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL TestLocateViews(XrSession, const XrViewLocateInfo*,
    XrViewState* state, uint32_t, uint32_t* count, XrView* views) {
  state->viewStateFlags = tracking_valid?
      XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT:0;
  *count = 2;
  for (int eye = 0; eye < 2; ++eye) {
    views[eye].pose.orientation.y=std::sin(head_yaw*.5f);
    views[eye].pose.orientation.w=std::cos(head_yaw*.5f);
    views[eye].pose.position.x = eye ? .032f : -.032f;
    views[eye].fov = view_fov[eye];
  }
  return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL TestLocateSpace(XrSpace, XrSpace, XrTime, XrSpaceLocation* out) {
  out->locationFlags = tracking_valid?
      XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT:0;
  out->pose.orientation.w = 1;
  out->pose.orientation.y=std::sin(head_yaw*.5f);
  out->pose.orientation.w=std::cos(head_yaw*.5f);
  out->pose.position.y = 1.6f;
  return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL TestAcquireSwapchainImage(XrSwapchain chain,
    const XrSwapchainImageAcquireInfo*, uint32_t* index) {
  const int eye = Side(chain);
  if (acquired[eye]) bad_order = true;
  acquired[eye] = true;
  ++acquisitions;
  *index = 0;
  return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL TestWaitSwapchainImage(XrSwapchain chain,
    const XrSwapchainImageWaitInfo* info) {
  const int eye = Side(chain);
  if (!acquired[eye]) bad_order = true;
  if (info->timeout > longest_wait) longest_wait = info->timeout;
  if (image_wait[eye] == XR_SUCCESS) ready[eye] = true;
  return image_wait[eye];
}
XRAPI_ATTR XrResult XRAPI_CALL TestReleaseSwapchainImage(XrSwapchain chain,
    const XrSwapchainImageReleaseInfo*) {
  const int eye = Side(chain);
  if (!acquired[eye] || !ready[eye]) bad_order = true;
  if (image_release[eye] == XR_SUCCESS) acquired[eye] = ready[eye] = false;
  return image_release[eye];
}
XRAPI_ATTR XrResult XRAPI_CALL TestEndFrame(XrSession, const XrFrameEndInfo* info) {
  if (!frame_begun || info->displayTime != predicted) bad_order = true;
  frame_begun = false;
  if (info->layerCount) {
    if (acquired[0] || acquired[1]) bad_order = true;
    if (info->layers[0]->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
      const auto* layer = reinterpret_cast<const XrCompositionLayerProjection*>(info->layers[0]);
      if (layer->viewCount != 2) bad_order = true;
      else {
        for (unsigned eye = 0; eye < 2; ++eye) last_projection_poses[eye] = layer->views[eye].pose;
        last_projection_pose = last_projection_poses[0];
      }
    }
    ++submitted;
  }
  return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL TestDestroySwapchain(XrSwapchain chain) {
  if (XR_FAILED(destroy_result[Side(chain)])) return destroy_result[Side(chain)];
  auto*& image = created_textures[Side(chain)];
  if (image) { image->Release(); image = nullptr; }
  acquired[Side(chain)] = ready[Side(chain)] = false;
  return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL TestDestroySpace(XrSpace) { return XR_SUCCESS; }
XRAPI_ATTR XrResult XRAPI_CALL TestEndSession(XrSession) {
  ++ended_sessions;if(frame_begun)bad_order=true;return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL TestResultToString(XrInstance, XrResult result,
    char out[XR_MAX_RESULT_STRING_SIZE]) {
  std::snprintf(out, XR_MAX_RESULT_STRING_SIZE, "result=%d", static_cast<int>(result));
  return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL TestEnumerateSwapchainFormats(XrSession, uint32_t capacity,
    uint32_t* count, int64_t* formats) {
  if (!allow_swapchain_creation) return XR_ERROR_RUNTIME_FAILURE;
  *count = 1;
  if (capacity) formats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL TestCreateReferenceSpace(XrSession,
    const XrReferenceSpaceCreateInfo* info, XrSpace* out) {
  *out = reinterpret_cast<XrSpace>(info->referenceSpaceType + 100);
  return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL TestCreateSwapchain(XrSession,
    const XrSwapchainCreateInfo* info, XrSwapchain* out) {
  const unsigned eye = next_created_eye++ % 2;
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = created_width[eye] = info->width;
  desc.Height = created_height[eye] = info->height;
  created_samples[eye] = info->sampleCount;
  desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
  desc.Format = static_cast<DXGI_FORMAT>(info->format);
  desc.BindFlags = D3D11_BIND_RENDER_TARGET;
  if (FAILED(dsvr::g_device->CreateTexture2D(&desc, nullptr, &created_textures[eye])))
    return XR_ERROR_RUNTIME_FAILURE;
  *out = chains[eye];
  return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL TestEnumerateSwapchainImages(XrSwapchain chain,
    uint32_t capacity, uint32_t* count, XrSwapchainImageBaseHeader* images) {
  *count = 1;
  if (capacity)
    reinterpret_cast<XrSwapchainImageD3D11KHR*>(images)->texture = created_textures[Side(chain)];
  return XR_SUCCESS;
}

namespace dsvr {
Config DiagnosticsCurrentConfig() { return config; }
bool DiagnosticsRequestConfig(const Config& next, bool) {
  requested_configs.push_back(next);
  config = next;
  return request_config_result;
}
bool ConfigValid(const Config& c) {
  return c.frame_limit_fps == 60 || c.frame_limit_fps == 72 ||
         c.frame_limit_fps == 80 || c.frame_limit_fps == 90 ||
         c.frame_limit_fps == 120 || c.frame_limit_fps == 144 ||
         c.frame_limit_fps == 240;
}
UserCalibration CalibrationCurrent() { return calibration_profile; }
bool CalibrationCapturesInput() { return calibration_visible; }
void LogWrite(LogLevel, const char*, int, const char*, ...) {}
bool CameraIsTitle() { return false; }
bool CameraGameplayReady() { return gameplay_ready; }
bool CameraNativeCutscene() { return native_cutscene; }
bool ModMenuVisible() { return mod_menu_visible; }
CalibrationStatus CalibrationGetStatus() {
  CalibrationStatus status{};
  status.screen_visible = calibration_visible;
  return status;
}
void CalibrationSample(const TrackingFrame& frame, bool trigger, bool gameplay,
                       bool floor_valid, float height, float fallback) {
  if (!frame.sequence) bad_order = true;
  ++calibration_samples;
  sampled_trigger = trigger; sampled_gameplay = gameplay; sampled_floor = floor_valid;
  sampled_height = height; sampled_fallback = fallback;
}
void InputAttachSession(void*, void*) {}
void InputInvalidateFrame() { comfort_motion = {}; }
ComfortMotion InputLatestComfortMotion() { return comfort_motion; }
void InputPollFrame(void*, const float*, std::uint64_t, long long) {}
bool DepthProbeInstall(ID3D11Device*) { return false; }
bool DepthProbeMainPair(ID3D11Texture2D*&, ID3D11Texture2D*&, std::uint32_t&,
                        std::uint32_t&, std::uint64_t&) { return false; }
void InputCopyTrackingSamples(HandSample hands[2], ArmTarget arms[2]) {
  hands[0] = hands[1] = {};
  hands[0].valid = true;
  hands[0].trigger_held = physical_trigger;
  hands[1].valid = true;
    hands[1].trigger_held = physical_trigger;
  hands[1].squeeze_held = physical_trigger;
  arms[0] = arms[1] = {};
}
bool InputLatestPad(VirtualPadState& out) {
  out = {};
  out.right_trigger = physical_trigger ? 255 : 0;
  return true;
}
void VrTraceRecord(VrTraceEvent) {}
}

// Model the natural game camera between Presents. The source image belongs to
// this packet; xrWaitFrame in the following submission predicts a newer one.
void SubmitMono(void* image) {
  dsvr::TrackingFrame source;
  if(dsvr::XrLatestTrackingFrame(source))
    dsvr::XrRecordCameraFrame(source,source.vfov_rad*std::clamp(config.fov_scale,.7f,1.3f));
  dsvr::XrSubmitMonoFrame(image);
}

int main() {
  using namespace dsvr;
  CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
      nullptr, 0, D3D11_SDK_VERSION, &g_device, nullptr, &g_context)));
  auto** original_vtable = *reinterpret_cast<void***>(g_context);
  // Include Context1's constant-buffer range methods used by the state guard.
  std::array<void*, 134> context_vtable{};
  std::memcpy(context_vtable.data(), original_vtable, sizeof(context_vtable));
  original_copy = reinterpret_cast<CopyFn>(context_vtable[47]);
  original_map = reinterpret_cast<MapFn>(context_vtable[14]);
  original_get_data = reinterpret_cast<GetDataFn>(context_vtable[29]);
  context_vtable[47] = reinterpret_cast<void*>(&CountCopy);
  context_vtable[14] = reinterpret_cast<void*>(&CountMap);
  context_vtable[29] = reinterpret_cast<void*>(&CountGetData);
  *reinterpret_cast<void***>(g_context) = context_vtable.data();
  config.headlook_enabled = true;
  g_instance=XR_NULL_HANDLE;g_session=XR_NULL_HANDLE;g_next_retry_ms=0;
  XrOnDeviceCaptured(g_device);
  CHECK(system_calls==1 && !g_session && session_creates==0);
  CHECK(!refresh_enabled && refresh_proc_lookups==0 && !g_get_display_refresh_rate);
  // Advertised-but-missing/failed procedure lookup is optional, never fatal.
  advertise_refresh=true;refresh_proc_available=false;
  g_instance=XR_NULL_HANDLE;g_next_retry_ms=0;XrOnDeviceCaptured(g_device);
  CHECK(refresh_enabled && refresh_proc_lookups==1 && !g_get_display_refresh_rate);
  refresh_proc_available=true;refresh_proc_result=XR_ERROR_FUNCTION_UNSUPPORTED;
  g_instance=XR_NULL_HANDLE;g_next_retry_ms=0;XrOnDeviceCaptured(g_device);
  CHECK(refresh_proc_lookups==2 && !g_get_display_refresh_rate);
  refresh_proc_result=XR_SUCCESS;
  g_instance=XR_NULL_HANDLE;g_next_retry_ms=0;XrOnDeviceCaptured(g_device);
  CHECK(g_get_display_refresh_rate && refresh_queries==0);
  // DSVR observes the physical panel rate for the dependency handoff but never
  // calls xrRequestDisplayRefreshRateFB or rewrites the configured cap.
  g_session = reinterpret_cast<XrSession>(2);
  config.frame_limit_fps = 90;
  config.auto_frame_limit_fps = false;
  physical_refresh = 120.0f;
  refresh_queries = 0; refresh_requests = 0; refresh_requested = 0.0f;
  QueryDisplayRefresh();
  CHECK(refresh_requests == 0 && g_display_refresh_hz == 120.0f);
  CHECK(config.frame_limit_fps == 90);
  physical_refresh = 90.0f;
  QueryDisplayRefresh();
  CHECK(refresh_requests == 0 && g_display_refresh_hz == 90.0f);
  physical_refresh = 120.0f;
  config.frame_limit_fps = 120;
  QueryDisplayRefresh();
  CHECK(refresh_requests == 0 && g_display_refresh_hz == 120.0f);
  physical_refresh = 90.0f;
  config.frame_limit_fps = 100;
  QueryDisplayRefresh();
  CHECK(refresh_requests == 0 && g_display_refresh_hz == 90.0f);
  config.auto_frame_limit_fps = true;
  requested_configs.clear();
  physical_refresh = 120.0f;
  QueryDisplayRefresh();
  CHECK(requested_configs.empty() && config.frame_limit_fps == 100);
  CHECK(refresh_requests == 0 && g_display_refresh_hz == 120.0f);
  physical_refresh = 72.0f;
  QueryDisplayRefresh();
  CHECK(requested_configs.empty() && config.frame_limit_fps == 100);
  CHECK(refresh_requests == 0 && g_display_refresh_hz == 72.0f);
  config.auto_frame_limit_fps = false;
  config.frame_limit_fps = 90;
  physical_refresh = 120.0f;
  const auto requests_before = refresh_requests;
  QueryDisplayRefresh();
  CHECK(refresh_requests == requests_before && g_display_refresh_hz == 120.0f);
  g_session = XR_NULL_HANDLE;
  refresh_queries = 0;
  setup_unavailable=false;g_next_retry_ms=0;
  XrOnDeviceCaptured(g_device);
  CHECK(g_session && session_creates==1 && refresh_queries==1 && g_display_refresh_hz==120.0);
  CHECK(refresh_requests == requests_before);
  config.frame_limit_fps = 60;  // restore the pre-existing STOPPING expectation

  g_session = reinterpret_cast<XrSession>(2);
  g_space = reinterpret_cast<XrSpace>(3);
  g_head_space = reinterpret_cast<XrSpace>(4);
  g_state = XR_SESSION_STATE_FOCUSED;
  g_running = true;
  g_xr_thread = GetCurrentThreadId();
  g_eye_width = g_eye_height = 64;
  g_eye_format = DXGI_FORMAT_R8G8B8A8_UNORM;
  CHECK(EnsureBlit());
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = desc.Height = 64;
  desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  ID3D11Texture2D* backbuffer = nullptr;
  CHECK(SUCCEEDED(g_device->CreateTexture2D(&desc, nullptr, &backbuffer)));
  ID3D11Texture2D* textures[2]{};
  for (int eye = 0; eye < 2; ++eye) {
    CHECK(SUCCEEDED(g_device->CreateTexture2D(&desc, nullptr, &textures[eye])));
    g_swapchain[eye] = chains[eye];
    g_images[eye].push_back({XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR, nullptr, textures[eye]});
    ID3D11RenderTargetView* rtv = nullptr;
    CHECK(SUCCEEDED(g_device->CreateRenderTargetView(textures[eye], nullptr, &rtv)));
    g_rtvs[eye].push_back(rtv);
  }

  ID3D11RenderTargetView* native_target=nullptr;
  CHECK(SUCCEEDED(g_device->CreateRenderTargetView(backbuffer,nullptr,&native_target)));
  g_context->OMSetRenderTargets(1,&native_target,nullptr);
  const D3D11_VIEWPORT native_viewport{3,5,41,37,.2f,.8f};
  g_context->RSSetViewports(1,&native_viewport);
  g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
  D3D11_BUFFER_DESC native_cb_desc{};
  native_cb_desc.ByteWidth=1024;native_cb_desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
  ID3D11Buffer* native_cb=nullptr;
  CHECK(SUCCEEDED(g_device->CreateBuffer(&native_cb_desc,nullptr,&native_cb)));
  g_context->PSSetConstantBuffers(0,1,&native_cb);
  ID3D11DeviceContext1* context1=nullptr;
  CHECK(SUCCEEDED(g_context->QueryInterface(__uuidof(ID3D11DeviceContext1),
                                            reinterpret_cast<void**>(&context1))));
  const UINT first_constant=16,constant_count=16;
  context1->PSSetConstantBuffers1(0,1,&native_cb,&first_constant,&constant_count);
  // During title/login there may be no follow-camera callback at all, so the
  // title-camera heuristic cannot be the only way to enable menu navigation.
  gameplay_ready=false;g_menu_debounce=MenuDebouncer{0};
  head_yaw=.2f;
  XrSubmitMonoFrame(backbuffer);
  CHECK(XrMenuPresentationActive());
  ID3D11RenderTargetView* restored_target=nullptr;
  g_context->OMGetRenderTargets(1,&restored_target,nullptr);
  CHECK(restored_target==native_target);if(restored_target)restored_target->Release();
  D3D11_VIEWPORT restored_viewport{};UINT viewport_count=1;
  g_context->RSGetViewports(&viewport_count,&restored_viewport);
  CHECK(viewport_count==1 && std::memcmp(&native_viewport,&restored_viewport,sizeof(native_viewport))==0);
  D3D11_PRIMITIVE_TOPOLOGY restored_topology{};g_context->IAGetPrimitiveTopology(&restored_topology);
  CHECK(restored_topology==D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
  ID3D11Buffer* restored_cb=nullptr;
  g_context->PSGetConstantBuffers(0,1,&restored_cb);
  CHECK(restored_cb==native_cb);if(restored_cb)restored_cb->Release();
  UINT got_first=0,got_count=0;
  restored_cb=nullptr;
  context1->PSGetConstantBuffers1(0,1,&restored_cb,&got_first,&got_count);
  CHECK(restored_cb==native_cb);if(restored_cb)restored_cb->Release();
  CHECK(got_first==first_constant && got_count==constant_count);
  CHECK(submitted == 1 && !acquired[0] && !acquired[1]);
  CHECK(calibration_samples == 1 && !sampled_gameplay && !sampled_trigger && !sampled_floor);
  const unsigned first_frame_copies = source_copies;
  // A normal periodic frame must never synchronously read GPU pixels back to
  // the CPU. This used to allocate/map two staging textures every 600 frames.
  g_submitted = 600;
  const unsigned maps_before = readback_maps;
  TrackingFrame rendered_pose{};CHECK(XrLatestTrackingFrame(rendered_pose));
  XrRecordCameraFrame(rendered_pose,rendered_pose.vfov_rad);
  head_yaw=.6f; // runtime advances AFTER the game has rendered the old camera
  gameplay_ready=true;
  XrSubmitMonoFrame(backbuffer);
  CHECK(!XrMenuPresentationActive());
  CHECK(std::fabs(last_projection_pose.orientation.y-rendered_pose.head_quat[1])<1e-6f);
  const unsigned periodic_readbacks = readback_maps - maps_before;
  std::printf("Mono work: first-frame source copies=%u, periodic CPU readbacks=%u\n",
              first_frame_copies, periodic_readbacks);
  CHECK(first_frame_copies == 1);
  CHECK(periodic_readbacks == 0);
  RenderMetrics metrics{};
  CHECK(XrRenderMetricsSnapshot(metrics));
  CHECK(metrics.source_tracking_sequence==rendered_pose.sequence);
  CHECK(metrics.target_tracking_sequence>metrics.source_tracking_sequence);
  CHECK(metrics.frame_sequence == 2 && metrics.submitted);
  CHECK(metrics.source_copies == 1 && metrics.eye_draws == 2);
  CHECK(metrics.source_width == 64 && metrics.source_height == 64);
  CHECK(metrics.eye_width == 64 && metrics.eye_height == 64);
  CHECK(metrics.cpu_frame_ms >= metrics.xr_wait_ms);
  CHECK(metrics.cpu_frame_ms >= metrics.source_copy_cpu_ms);
    for (unsigned eye = 0; eye < 2; ++eye) {
      const auto& pose = last_projection_poses[eye];
      CHECK(std::fabs(pose.orientation.y - rendered_pose.head_quat[1]) < 1e-6f);
      CHECK(std::fabs(pose.orientation.w - rendered_pose.head_quat[3]) < 1e-6f);
      CHECK(std::fabs(pose.position.x - rendered_pose.head_pos[0]) < 1e-6f);
      CHECK(std::fabs(pose.position.y - rendered_pose.head_pos[1]) < 1e-6f);
      CHECK(std::fabs(pose.position.z - rendered_pose.head_pos[2]) < 1e-6f);
    }
    // Reject bad source provenance without losing the next camera prediction.
    // Recovery needs a natural camera receipt, not a diagnostic sequence tag.
    for (unsigned mutation = 0; mutation < 13; ++mutation) {
      TrackingFrame source{};
      CHECK(XrLatestTrackingFrame(source));
      float vfov = source.vfov_rad;
      switch (mutation) {
        case 0: source.sequence = 0; break;
        case 1: ++source.space_epoch; break;
        case 2: source.captured_ms = GetTickCount64() - 101; break;
        case 3: source.captured_ms = GetTickCount64() + 1000; break;
        case 4: source.predicted_time_ns = 0; break;
        case 5: source.predicted_time_ns = predicted + 1'000'000'000; break;
        case 6: source.focused = false; break;
        case 7: source.orientation_valid = false; break;
        case 8: source.position_valid = false; break;
        case 9: vfov = .5f; break;
        case 10: vfov = 3.1f; break;
        case 11: source.fov_aspect = 0; break;
        default: break;
      }
      XrRecordCameraFrame(source, vfov);
      if (mutation == 12) XrRecordCameraSequence(source.sequence);
      const auto before_submit = submitted, before_copy = source_copies;
      XrSubmitMonoFrame(backbuffer);
      CHECK(submitted == before_submit && source_copies == before_copy);
      CHECK(!frame_begun && !acquired[0] && !acquired[1]);
      TrackingFrame fresh{};
      CHECK(XrLatestTrackingFrame(fresh) && fresh.sequence > source.sequence);
      XrRecordCameraFrame(fresh, fresh.vfov_rad);
      XrSubmitMonoFrame(backbuffer);
      CHECK(submitted == before_submit + 1);
      CHECK(XrRenderMetricsSnapshot(metrics) && metrics.submitted);
      CHECK(metrics.source_tracking_sequence == fresh.sequence);
    }
  // A later missing camera sample (load/death/tracking transition) must
  // force theater: loading and non-3D cutscenes are never immersive.
  // The debouncer needs 150 ms of FrontEnd readings: the fixture clock
  // is real time, so sleep past the window before asserting the flip.
  // g_gameplay_observed latches true once gameplay was seen, so reset it
  // by restarting the presentation state for this transition.
  gameplay_ready=false;
  g_menu_debounce = MenuDebouncer{0};
  g_gameplay_observed = false;
  SubmitMono(backbuffer);
  CHECK(XrMenuPresentationActive());
  Sleep(200);
  SubmitMono(backbuffer);
  CHECK(XrMenuPresentationActive());
  CHECK(XrRenderMetricsSnapshot(metrics) && metrics.theater);
  gameplay_ready=true;
  // Even a GPU that never completes a timestamp query cannot stall rendering
  // or grow the query ring: at most four single non-flushing polls per frame.
  force_queries_pending = true;
  for (unsigned frame = 0; frame < 130; ++frame) {
    const auto before = query_polls;
    SubmitMono(backbuffer);
    CHECK(query_polls - before <= 4);
  }
  CHECK(!query_flush_requested && readback_maps == maps_before);
  force_queries_pending = false;
  // Flush only in the TEST to complete actual WARP queries, then let the
  // production path collect them without any flush or blocking readback.
  for (unsigned attempt = 0; attempt < 30; ++attempt) {
    g_context->Flush();
    Sleep(1);
    SubmitMono(backbuffer);
    CHECK(XrRenderMetricsSnapshot(metrics));
    if (metrics.gpu_valid) break;
  }
  CHECK(metrics.gpu_valid && metrics.gpu_frame_sequence < metrics.frame_sequence);
  CHECK(metrics.gpu_copy_ms >= 0 && metrics.gpu_blit_ms >= 0);
  CHECK(!query_flush_requested);
  // Exercise the shipped shader and envelope through actual mono submission.
  // Readback is TEST ONLY; the production compositor never maps GPU pixels.
  {
    const auto saved=config;
    const float white[4]{1,1,1,1};g_context->ClearRenderTargetView(native_target,white);
    D3D11_TEXTURE2D_DESC read_desc=desc;
    read_desc.Usage=D3D11_USAGE_STAGING;read_desc.BindFlags=0;
    read_desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* readback=nullptr;
    CHECK(SUCCEEDED(g_device->CreateTexture2D(&read_desc,nullptr,&readback)));
    const auto pixel = [&](int eye, unsigned x, unsigned y) {
      g_context->CopyResource(readback,textures[eye]);
      D3D11_MAPPED_SUBRESOURCE mapped{};
      if(FAILED(g_context->Map(readback,0,D3D11_MAP_READ,0,&mapped)))return -1;
      const int value=static_cast<const unsigned char*>(mapped.pData)[y*mapped.RowPitch+x*4];
      g_context->Unmap(readback,0);return value;
    };
    comfort_motion={.7f,0};config.comfort_vignette=true;config.vignette_strength=1;
    for(unsigned i=0;i<45;++i)SubmitMono(backbuffer);
    CHECK(pixel(0,32,32)>250 && pixel(1,32,32)>250);
    const int left=pixel(0,2,32),right=pixel(1,2,32);
    CHECK(left>=0 && left<200 && std::abs(left-right)<=1);
    CHECK(pixel(0,12,32)>left); // feathered aperture, not a hard rectangle
    comfort_motion={1,0};
    view_fov[0]={-1.1f,.7f,.9f,-.8f};view_fov[1]={-.7f,1.1f,.9f,-.8f};
    for(unsigned i=0;i<45;++i)SubmitMono(backbuffer);
    CHECK(pixel(0,44,35)>250 && pixel(1,19,35)>250); // asymmetric optical axes
    CHECK(pixel(0,25,35)<60 && pixel(1,38,35)<60);
    config.comfort_vignette=false;SubmitMono(backbuffer);
    CHECK(pixel(0,25,35)>250 && pixel(1,38,35)>250);
    comfort_motion={};config.comfort_vignette=true;
    for(unsigned i=0;i<5;++i)SubmitMono(backbuffer);
    CHECK(pixel(0,25,35)>250); // no stick/turn demand after Off
    view_fov[0]=view_fov[1]={-1,1,1,-1};
    config=saved;readback->Release();
  }
  // The explicit DSVR menu forces the head-anchored canvas (one eye drawn). The
  // calibration overlay does NOT: it is a panel drawn into the flat game image,
  // so it keeps both eyes and the readable theater rect. Calibration readiness
  // still comes from the natural gameplay observation, including when that
  // overlay itself is visible. Loading (no gameplay sample) forces theater too,
  // and the trigger+stick gesture resizes it.
  g_menu_debounce = MenuDebouncer{0};
  calibration_visible = true;
  gameplay_ready = false;
  native_cutscene = true;
  physical_trigger = true;
  config.eye_height = 1.73f;config.eye_height_override=true;
  calibration_profile.valid=true;calibration_profile.eye_height_m=1.71f;
  g_floor_space = reinterpret_cast<XrSpace>(5);
  SubmitMono(backbuffer);
  CHECK(sampled_trigger && !sampled_gameplay && sampled_floor);
  CHECK(std::fabs(sampled_height - 1.6f) < .001f);
  CHECK(std::fabs(sampled_fallback - 1.71f) < .001f);
  CHECK(XrRenderMetricsSnapshot(metrics) && metrics.theater && metrics.eye_draws == 2);
  calibration_visible = false;
  mod_menu_visible = true;
  SubmitMono(backbuffer);
  CHECK(XrRenderMetricsSnapshot(metrics) && metrics.theater && metrics.eye_draws == 1);
  mod_menu_visible = false;
  // Cutscene without gameplay: theater stays forced even with the menu closed.
  SubmitMono(backbuffer);
  CHECK(XrRenderMetricsSnapshot(metrics) && metrics.theater && metrics.eye_draws == 1);
  // Trigger held + right stick up/down resizes the theater window live.
  {
    const float before = config.mono_screen_fov_deg;
    physical_trigger = true;
    SubmitMono(backbuffer);
    CHECK(XrRenderMetricsSnapshot(metrics) && metrics.theater);
    physical_trigger = false;
  }
  native_cutscene = false;
  gameplay_ready = true;
  physical_trigger = false;
  SubmitMono(backbuffer);
  CHECK(XrRenderMetricsSnapshot(metrics) && !metrics.theater && metrics.eye_draws == 2);
  // Project the runtime AIM pose through the shipped XR adapter. Deliberately
  // point the grip elsewhere: a grip-based ray would miss every sample below.
  {
      const auto saved_config=config;const bool saved_local=g_local_space;
      TrackingFrame saved_tracking{};CHECK(XrLatestTrackingFrame(saved_tracking));
    g_local_space=true;g_menu_active=true;g_menu_anchored=true;
    for(float distance:{1.f,2.75f,4.f})for(float yaw:{0.f,.7f,1.5707963f,2.7f}) {
      config.menu_distance_m=distance;
      g_menu_anchor=MakeMenuAnchor(.2f,1.6f,-.3f,yaw,distance);
      const float width=MenuWidth(distance,config.menu_reading_fov_deg);
      const float height=width/(1280.f/720.f);
      for(float u:{.1f,.5f,.9f})for(float v:{.1f,.5f,.9f}) {
        const Vec3 normal{std::sin(yaw),0,std::cos(yaw)};
        const Vec3 right{std::cos(yaw),0,-std::sin(yaw)};
        const Vec3 target=VAdd({g_menu_anchor.x,g_menu_anchor.y,g_menu_anchor.z},
            VAdd(VScale(right,(u-.5f)*width),Vec3{0,(.5f-v)*height,0}));
        const Vec3 origin=VAdd(target,VAdd(VScale(normal,2.f),Vec3{0,-.5f,0}));
        TrackingFrame pointing{};pointing.focused=pointing.orientation_valid=pointing.position_valid=true;
        pointing.predicted_time_ns=1;pointing.captured_ms=GetTickCount64();
        auto& hand=pointing.hand[1];hand.valid=hand.aim_valid=hand.trigger_held=true;
        hand.quat[0]=-.70710678f;hand.quat[3]=.70710678f;
        const float pitch=std::atan2(.5f,2.f);
        hand.aim_quat[0]=std::cos(yaw*.5f)*std::sin(pitch*.5f);
        hand.aim_quat[1]=std::sin(yaw*.5f)*std::cos(pitch*.5f);
        hand.aim_quat[2]=-std::sin(yaw*.5f)*std::sin(pitch*.5f);
        hand.aim_quat[3]=std::cos(yaw*.5f)*std::cos(pitch*.5f);
        hand.aim_pos[0]=origin.x;hand.aim_pos[1]=origin.y;hand.aim_pos[2]=origin.z;
        g_tracking_frames.Publish(pointing);
        const auto pointer=XrMenuPointerInput(1280,720);
        CHECK(pointer.valid && pointer.select);
        CHECK(std::fabs(pointer.x-u*1280)<.01f && std::fabs(pointer.y-v*720)<.01f);
      }
    }
    config=saved_config;g_local_space=saved_local;g_menu_active=false;g_menu_anchored=false;
    g_tracking_frames.Publish(saved_tracking); // pointer-only packets are not native camera predictions
  }
  // A lost/failed source SRV must rebuild on the next accepted frame instead
  // of permanently submitting a missing or stale source image.
  g_blit_srv->Release();
  g_blit_srv = nullptr;
  const auto before_source_recovery = submitted;
  SubmitMono(backbuffer);
  CHECK(g_blit_srv && submitted == before_source_recovery + 1);
  // Each failed wait must retain the acquired image and prevent reacquisition
  // until a successful wait/release. A timeout is not a ready image.
  for (const auto failure : {XR_ERROR_RUNTIME_FAILURE, XR_TIMEOUT_EXPIRED}) {
    const unsigned before_submitted = submitted;
    image_wait[0] = failure;
    SubmitMono(backbuffer);
    CHECK(submitted == before_submitted && acquired[0] && !ready[0]);
    TrackingFrame tracking{};
    CHECK(XrLatestTrackingFrame(tracking)); // valid pose is independent of GPU image readiness
    const auto last_tracking_sequence=tracking.sequence;
    const auto before_acquires = acquisitions, before_waits = frame_waits;
    SubmitMono(backbuffer);
    CHECK(acquisitions == before_acquires && frame_waits == before_waits);
    CHECK(XrLatestTrackingFrame(tracking) && tracking.sequence==last_tracking_sequence);
    image_wait[0] = XR_SUCCESS;
    SubmitMono(backbuffer);
    CHECK(submitted == before_submitted + 1 && !acquired[0] && !acquired[1]);
  }
  // A failed release cannot submit an eye pair or acquire over its held image.
  image_release[1] = XR_ERROR_RUNTIME_FAILURE;
  const unsigned before_submitted = submitted;
  SubmitMono(backbuffer);
  CHECK(submitted == before_submitted && acquired[1] && ready[1]);
  const auto before_acquires = acquisitions;
  SubmitMono(backbuffer);
  CHECK(acquisitions == before_acquires);
  image_release[1] = XR_SUCCESS;
  SubmitMono(backbuffer);
  CHECK(submitted == before_submitted + 1 && !bad_order);
  CHECK(longest_wait <= 5'000'000);
  // Runtime rendering suppression must not turn a fresh, valid tracked pose
  // into apparent tracking loss. Actual invalid head tracking still clears it.
  TrackingFrame preserved{};CHECK(XrLatestTrackingFrame(preserved));
  should_render=false;SubmitMono(backbuffer);
  TrackingFrame suppressed{};CHECK(XrLatestTrackingFrame(suppressed) && suppressed.sequence==preserved.sequence);
  should_render=true;tracking_valid=false;SubmitMono(backbuffer);
  CHECK(!XrLatestTrackingFrame(suppressed));
  tracking_valid=true;SubmitMono(backbuffer);CHECK(XrLatestTrackingFrame(suppressed));
  // Exercise the actual mono -> PublishRate -> XrEffectiveRate path, not just
  // the policy helper. Application reprojection must not masquerade as 60 Hz.
  {
    EffectiveRate rate{};
    const auto saved_config=config;
    config.frame_limit_fps=90;application_period=16'666'667;
    g_rate_stabilizer=RateStabilizer{};
    const auto queries_before=refresh_queries;
    for(int i=0;i<6;++i)SubmitMono(backbuffer);
    CHECK(XrEffectiveRate(rate) && rate.from_display && rate.effective_fps==120);
    CHECK(rate.display_hz==120.0 && std::fabs(rate.runtime_hz-60.0)<.01);
    CHECK(refresh_queries==queries_before); // cache, not a per-frame API poll
    event_refresh=80.0f;refresh_event_pending=true;
    for(int i=0;i<kStableRateSamples;++i)SubmitMono(backbuffer);
    CHECK(XrEffectiveRate(rate) && rate.from_display && rate.effective_fps==80);
    CHECK(rate.display_hz==80.0 && refresh_queries==queries_before);
    // Invalid FB values must clear, not pin the last physical refresh forever.
    const float invalid[]{0.0f,-90.0f,NAN,INFINITY,1.0f};
    for(float hz:invalid) {
      event_refresh=hz;refresh_event_pending=true;
      for(int i=0;i<kStableRateSamples;++i)SubmitMono(backbuffer);
      CHECK(XrEffectiveRate(rate));
              
              CHECK(!rate.from_display && rate.effective_fps==60);
    }
    // Lost events trigger one resync; failed query never uses a written value.
    physical_refresh=144.0f;events_lost_pending=true;
    for(int i=0;i<kStableRateSamples;++i)SubmitMono(backbuffer);
    CHECK(refresh_queries==queries_before+1);
    CHECK(XrEffectiveRate(rate) && rate.from_display && rate.effective_fps==144);
    refresh_query_result=XR_ERROR_RUNTIME_FAILURE;events_lost_pending=true;
    for(int i=0;i<kStableRateSamples;++i)SubmitMono(backbuffer);
    CHECK(refresh_queries==queries_before+2 && g_display_refresh_hz==0.0);
    CHECK(XrEffectiveRate(rate));
            
            CHECK(!rate.from_display && rate.effective_fps==60);
    refresh_query_result=XR_SUCCESS;
    for(float hz:invalid) {
      physical_refresh=hz;events_lost_pending=true;
      for(int i=0;i<kStableRateSamples;++i)SubmitMono(backbuffer);
      CHECK(XrEffectiveRate(rate));
              
              CHECK(!rate.from_display && rate.effective_fps==60);
    }
    // Extension absent: same runtime frame path, predicted fallback, no query.
    g_get_display_refresh_rate=nullptr;g_display_refresh_hz=0.0;
    const auto absent_queries=refresh_queries;
    events_lost_pending=true;event_refresh=120.0f;refresh_event_pending=true;
    for(int i=0;i<kStableRateSamples;++i)SubmitMono(backbuffer);
    CHECK(XrEffectiveRate(rate));
            
            CHECK(!rate.from_display && rate.effective_fps==60);
    CHECK(refresh_queries==absent_queries);
    g_get_display_refresh_rate=&TestGetDisplayRefreshRateFB;
    physical_refresh=120.0f;QueryDisplayRefresh();
    application_period=11'111'111;config=saved_config;
    g_rate_stabilizer=RateStabilizer{};
  }
  // A sleeping headset must not hold up the actual native render callback.
  inactive_wait_entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  inactive_wait_release=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  block_inactive_wait=true;should_render=false;g_state=XR_SESSION_STATE_SYNCHRONIZED;
  const auto inactive_start=GetTickCount64();
  SubmitMono(backbuffer);
  CHECK(GetTickCount64()-inactive_start<100);
  CHECK(WaitForSingleObject(inactive_wait_entered,1000)==WAIT_OBJECT_0);
  CHECK(XrMenuPresentationActive());
  // A visible event may arrive while the empty frame still owns the runtime.
  // Do not begin a graphics frame or destroy its session before that handoff.
  g_state=XR_SESSION_STATE_FOCUSED;
  const auto graphics_waits=frame_waits;
  SubmitMono(backbuffer);
  CHECK(frame_waits==graphics_waits && !g_inactive_pump.PauseAndIdle());
  SetEvent(inactive_wait_release);
  const auto handoff_deadline=GetTickCount64()+1000;
  while(!g_inactive_pump.PauseAndIdle() && GetTickCount64()<handoff_deadline)Sleep(1);
  CHECK(g_inactive_pump.PauseAndIdle());
  block_inactive_wait=false;should_render=true;
  CloseHandle(inactive_wait_entered);CloseHandle(inactive_wait_release);
  SubmitMono(backbuffer);
  CHECK(!XrMenuPresentationActive() && XrLatestTrackingFrame(suppressed));
  // Failed owner destruction must retain that eye's lease and resources;
  // successful partial teardown must not erase the remaining owner's state.
  for (int entry = 0; entry < 3; ++entry) {
    if (entry) {
      g_head_space = reinterpret_cast<XrSpace>(4);
      g_eye_width = g_eye_height = 64;
      for (int eye = 0; eye < 2; ++eye) {
        g_swapchain[eye] = chains[eye];
        g_images[eye].push_back({XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR, nullptr, textures[eye]});
        ID3D11RenderTargetView* rtv = nullptr;
        CHECK(SUCCEEDED(g_device->CreateRenderTargetView(textures[eye], nullptr, &rtv)));
        g_rtvs[eye].push_back(rtv);
      }
    }
    image_wait[1] = XR_TIMEOUT_EXPIRED;
    SubmitMono(backbuffer);
    CHECK(acquired[1] && !ready[1]);
    destroy_result[1] = XR_ERROR_RUNTIME_FAILURE;
    const auto cleanup_deadline = GetTickCount64() + 1000;
    do {
      CHECK(!DestroySwapchains());
      if (!g_swapchain[0]) break;
      Sleep(1);  // Pump bounded teardown while the actual WARP GPU marker completes.
    } while (GetTickCount64() < cleanup_deadline);
    CHECK(!g_swapchain[0] && g_swapchain[1] == chains[1]);
    CHECK(g_mono_images[1].chain == chains[1] && !g_mono_images[1].ready);
    CHECK(g_rtvs[0].empty() && !g_rtvs[1].empty() && g_head_space);
    CHECK(g_destroy_pending && g_destroy_query);
    CHECK(!EnsureSwapchains(backbuffer));
    CHECK(g_swapchain[1] == chains[1] && g_mono_images[1].chain == chains[1]);
    destroy_result[1] = XR_SUCCESS;
    // Resume through public entry points while that image STILL times out.
    // Cleanup must run before waiting on the retained image again.
    if (entry == 0) XrOnPresentFrame();
    if (entry == 1) SubmitMono(backbuffer);
    if (entry == 2) {
      NativeStereoFrame frame{};
      CHECK(!XrBeginNativeStereoFrame(backbuffer, frame));
    }
    CHECK(!g_swapchain[1] && !g_mono_images[1].chain && !g_head_space && !acquired[1]);
    CHECK(!g_destroy_pending && !g_destroy_query);
    image_wait[1] = XR_SUCCESS;
  }
  // Exercise production swapchain construction, not just resolution math.
  // 100 percent must keep the runtime recommendation; 200 percent must
  // obey the runtime maximum. Changing live config alone keeps startup scale.
  // MSAA 4x is the default: swapchain creation must request 4 samples.
  allow_swapchain_creation = true;
  g_eye_scale_percent = 0;  // fresh startup after the earlier failure cases
  g_rec_width = g_rec_height = 100;
  g_max_eye_width = 120; g_max_eye_height = 110;
  config.render_scale_percent = 100;
  config.aa_mode = 3;  // MSAA 4x default
  CHECK(EnsureSwapchains(backbuffer));
  CHECK(created_width[0] == 100 && created_height[0] == 100);
  CHECK(created_width[1] == 100 && created_height[1] == 100);
  CHECK(created_samples[0] == 4 && created_samples[1] == 4);
  config.render_scale_percent = 200;
  CHECK(EnsureSwapchains(backbuffer) && g_eye_width == 100);
  auto finish_teardown = [&]() {
    const auto deadline = GetTickCount64() + 1000;
    while (!DestroySwapchains() && GetTickCount64() < deadline) Sleep(1);
    return !g_destroy_pending;
  };
  CHECK(finish_teardown());
  g_eye_scale_percent = 0;  // emulate the next process's startup preference
  CHECK(EnsureSwapchains(backbuffer));
  CHECK(created_width[0] == 120 && created_height[0] == 110);
  CHECK(created_width[1] == 120 && created_height[1] == 110);
  CHECK(finish_teardown());
  // STOPPING while the invisible worker owns a frame must defer EndSession.
  inactive_wait_entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  inactive_wait_release=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  block_inactive_wait=true;should_render=false;g_state=XR_SESSION_STATE_SYNCHRONIZED;
  XrOnPresentFrame();CHECK(WaitForSingleObject(inactive_wait_entered,1000)==WAIT_OBJECT_0);
  next_session_state=XR_SESSION_STATE_STOPPING;
  XrOnPresentFrame();CHECK(!XrSessionRunning() && ended_sessions==0);
  CHECK(g_display_refresh_hz==0.0);
  EffectiveRate stopped_rate{};CHECK(XrEffectiveRate(stopped_rate));
  CHECK(!stopped_rate.from_runtime && !stopped_rate.from_display && stopped_rate.effective_fps==0);
  CHECK(!DestroySwapchains()); // no resource teardown may race that frame
  SetEvent(inactive_wait_release);
  const auto stop_deadline=GetTickCount64()+1000;
  while(!g_inactive_pump.PauseAndIdle() && GetTickCount64()<stop_deadline)Sleep(1);
  CHECK(g_inactive_pump.PauseAndIdle());
  block_inactive_wait=false;
  XrOnPresentFrame();CHECK(ended_sessions==1 && !bad_order);
  // The same native session handle can restart after deferred cleanup.
  const auto restart_queries=refresh_queries;
  next_session_state=XR_SESSION_STATE_READY;XrOnPresentFrame();
  CHECK(begun_sessions==1 && XrSessionRunning());
  CHECK(refresh_queries==restart_queries+1 && g_display_refresh_hz==120.0);
  next_session_state=XR_SESSION_STATE_FOCUSED;XrOnPresentFrame();
  const auto restart_deadline=GetTickCount64()+1000;
  while(!g_inactive_pump.PauseAndIdle() && GetTickCount64()<restart_deadline)Sleep(1);
  CHECK(g_inactive_pump.PauseAndIdle());
  CHECK(g_inactive_pump.StopAndWait(1000));
  CloseHandle(inactive_wait_entered);CloseHandle(inactive_wait_release);
  // Both session and instance loss clear the physical authority immediately.
  next_session_state=XR_SESSION_STATE_LOSS_PENDING;XrOnPresentFrame();
  CHECK(!XrSessionRunning() && g_display_refresh_hz==0.0);
  CHECK(XrEffectiveRate(stopped_rate) && !stopped_rate.from_runtime && !stopped_rate.from_display);
  event_refresh=144.0f;refresh_event_pending=true;XrOnPresentFrame();
  CHECK(g_display_refresh_hz==0.0); // late event cannot revive a lost session
  g_state=XR_SESSION_STATE_FOCUSED;g_running=true;QueryDisplayRefresh();
  instance_loss_pending=true;XrOnPresentFrame();
  CHECK(!XrSessionRunning() && g_display_refresh_hz==0.0);
  CHECK(XrEffectiveRate(stopped_rate) && !stopped_rate.from_display);
  g_state=XR_SESSION_STATE_FOCUSED;g_running=true;QueryDisplayRefresh();
  poll_error=XR_ERROR_INSTANCE_LOST;XrOnPresentFrame();
  CHECK(!XrSessionRunning() && g_display_refresh_hz==0.0);
  CHECK(XrEffectiveRate(stopped_rate) && !stopped_rate.from_runtime);
  backbuffer->Release();
  for (auto* texture : textures) texture->Release();
  g_context->ClearState();
  native_target->Release();
  native_cb->Release();
  context1->Release();
  g_context->Flush();
  auto release = [](auto*& object) { if (object) object->Release(); object = nullptr; };
  release(g_blit_vs); release(g_blit_ps); release(g_blit_sampler);
  release(g_blit_blend); release(g_blit_depth); release(g_blit_raster);
  release(g_blit_constants);
  release(g_blit_srv); release(g_stage);
  *reinterpret_cast<void***>(g_context) = original_vtable;
  g_context->Release();
  g_device->Release();
  std::puts("PASS actual mono image error/timeout/release recovery (fake XR, headless WARP)");
}
