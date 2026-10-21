// Real add-on DLL registration/callback boundary, with a fake API-18 host.
// Optional positive test uses WARP textures and a test-only core fixture; no
// game, XR runtime/session or production core startup is invoked.
#include <windows.h>
#include <reshade_events.hpp>
#include <d3d11.h>
#include <wrl/client.h>
#include "reshade_device_stub.h"
#include "reshade_fixture.h"

#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

static bool supported = false;
static unsigned registrations = 0, unregistrations = 0, events = 0;
static reshade::addon_event_traits<reshade::addon_event::present>::decl present = nullptr;
static reshade::addon_event_traits<reshade::addon_event::destroy_swapchain>::decl destroy = nullptr;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); \
  return 1; } } while (0)

extern "C" __declspec(dllexport) bool ReShadeRegisterAddon(HMODULE, uint32_t api) {
  ++registrations;
  return supported && api == 18;
}
extern "C" __declspec(dllexport) void ReShadeUnregisterAddon(HMODULE) {
  ++unregistrations;
}
extern "C" __declspec(dllexport) void ReShadeRegisterEvent(
    reshade::addon_event event, void* callback) {
  ++events;
  if (event == reshade::addon_event::present)
    present = reinterpret_cast<decltype(present)>(callback);
  if (event == reshade::addon_event::destroy_swapchain)
    destroy = reinterpret_cast<decltype(destroy)>(callback);
}
extern "C" __declspec(dllexport) void ReShadeUnregisterEvent(
    reshade::addon_event event, void* callback) {
  if (event == reshade::addon_event::present &&
      callback == reinterpret_cast<void*>(present)) { present = nullptr; --events; }
  if (event == reshade::addon_event::destroy_swapchain &&
      callback == reinterpret_cast<void*>(destroy)) { destroy = nullptr; --events; }
}

class MissingDeviceSwapchain final : public reshade::api::swapchain {
 public:
  uint64_t get_native() const override { return 0; }
  void get_private_data(const uint8_t[16], uint64_t* data) const override { *data = 0; }
  void set_private_data(const uint8_t[16], uint64_t) override {}
  reshade::api::device* get_device() override { return nullptr; }
  void* get_hwnd() const override { return nullptr; }
  reshade::api::resource get_back_buffer(uint32_t) override { return {0}; }
  uint32_t get_back_buffer_count() const override { return 0; }
  uint32_t get_current_back_buffer_index() const override { return 0; }
  bool check_color_space_support(reshade::api::color_space) const override { return false; }
  reshade::api::color_space get_color_space() const override {
    return reshade::api::color_space::unknown;
  }
};

using Microsoft::WRL::ComPtr;
static bool MakeTexture(ID3D11Device* device, unsigned width, unsigned height,
                        unsigned pixel, ComPtr<ID3D11Texture2D>& out) {
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = width; desc.Height = height;
  desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  std::vector<unsigned> pixels(width * height, pixel);
  D3D11_SUBRESOURCE_DATA data{pixels.data(), width * 4, 0};
  return SUCCEEDED(device->CreateTexture2D(&desc, &data, out.GetAddressOf()));
}

static ULONG References(IUnknown* object) {
  object->AddRef(); return object->Release();
}

static int PositiveFrame(const wchar_t* fixture_path) {
  CHECK(!GetModuleHandleW(L"DSVR.dll"));
  HWND window = CreateWindowExW(0, L"STATIC", L"DSVR offline texture test",
      WS_POPUP, 0, 0, 16, 8, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  CHECK(window);
  ComPtr<ID3D11Device> native;
  CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
      nullptr, 0, D3D11_SDK_VERSION, native.GetAddressOf(), nullptr, nullptr)));
  ComPtr<ID3D11Texture2D> images[2];
  CHECK(MakeTexture(native.Get(), 16, 8, 0xff113377, images[0]));
  CHECK(MakeTexture(native.Get(), 16, 8, 0xff446688, images[1]));
  reshade_test::Device sdk_device;
  reshade_test::Swapchain swap;
  swap.device = &sdk_device; swap.window = window;
  for (unsigned i = 0; i < 2; ++i)
    swap.images[i].handle = reinterpret_cast<uint64_t>(images[i].Get());
  swap.current = 1;  // detect accidental use of buffer zero
  present(nullptr, &swap, nullptr, nullptr, 0, nullptr);
  CHECK(swap.reads == 0 && !GetModuleHandleW(L"DSVR.dll"));

  HMODULE fixture = LoadLibraryW(fixture_path);
  CHECK(fixture && fixture == GetModuleHandleW(L"DSVR.dll"));
  auto start = reinterpret_cast<bool(*)()>(GetProcAddress(fixture, "FixtureStart"));
  auto read = reinterpret_cast<void(*)(ReShadeFixtureSnapshot*)>(
      GetProcAddress(fixture, "FixtureSnapshot"));
  CHECK(start && read);
  auto snapshot = [&] { ReShadeFixtureSnapshot s; read(&s); return s; };
  present(nullptr, &swap, nullptr, nullptr, 0, nullptr);
  CHECK(snapshot().submissions == 0);  // fixture readiness also starts false
  CHECK(start());
  present(nullptr, &swap, nullptr, nullptr, 0, nullptr);
  auto s = snapshot();
  CHECK(s.captures == 1 && s.polls == 1 && s.submissions == 1 && s.readbacks == 1);
  CHECK(s.image == reinterpret_cast<std::uintptr_t>(images[1].Get()));
  CHECK(s.device == reinterpret_cast<std::uintptr_t>(native.Get()));
  CHECK(s.swapchain == reinterpret_cast<std::uintptr_t>(&swap));
  CHECK(s.thread == GetCurrentThreadId());
  CHECK(s.pixel == 0xff446688 && s.width == 16 && s.height == 8);
  CHECK(s.stamp.generation == 1 && s.stamp.ordinal == 1);
  CHECK(swap.last_index == 1);
  const auto refs0 = References(images[0].Get()), refs1 = References(images[1].Get());
  for (unsigned i = 0; i < 16; ++i) {
    swap.current = i % 2;
    present(nullptr, &swap, nullptr, nullptr, 0, nullptr);
    s = snapshot();
    CHECK(s.pixel == (swap.current ? 0xff446688u : 0xff113377u));
    CHECK(s.submissions == i + 2 && s.readbacks == i + 2);
    CHECK(s.stamp.ordinal == i + 2 && s.stamp.generation == 1);
  }
  CHECK(References(images[0].Get()) == refs0 && References(images[1].Get()) == refs1);
  const unsigned delivered = snapshot().submissions;

  // Unsupported APIs/windowless/empty sources must be rejected before native
  // interpretation. No speculative cast of get_native() is permitted by stubs.
  auto reads = swap.reads;
  sdk_device.api = reshade::api::device_api::d3d12;
  present(nullptr, &swap, nullptr, nullptr, 0, nullptr);
  CHECK(swap.reads == reads && snapshot().submissions == delivered);
  sdk_device.api = reshade::api::device_api::d3d11;
  swap.window = nullptr;
  present(nullptr, &swap, nullptr, nullptr, 0, nullptr);
  CHECK(swap.reads == reads && snapshot().submissions == delivered);
  swap.window = window;
  swap.count = 0;
  present(nullptr, &swap, nullptr, nullptr, 0, nullptr);
  CHECK(swap.reads == reads && snapshot().submissions == delivered);
  swap.count = 2; swap.current = 2;
  present(nullptr, &swap, nullptr, nullptr, 0, nullptr);
  CHECK(swap.reads == reads && snapshot().submissions == delivered);
  swap.current = 0;
  const auto image0 = swap.images[0]; swap.images[0] = {0};
  present(nullptr, &swap, nullptr, nullptr, 0, nullptr);
  CHECK(snapshot().submissions == delivered);
  swap.images[0] = image0;
  swap.window = GetDesktopWindow();
  present(nullptr, &swap, nullptr, nullptr, 0, nullptr);
  CHECK(snapshot().submissions == delivered);
  swap.window = window;
  ComPtr<ID3D11Buffer> buffer;
  D3D11_BUFFER_DESC bd{}; bd.ByteWidth = 16;
  CHECK(SUCCEEDED(native->CreateBuffer(&bd, nullptr, buffer.GetAddressOf())));
  swap.images[0].handle = reinterpret_cast<uint64_t>(buffer.Get());
  present(nullptr, &swap, nullptr, nullptr, 0, nullptr);
  CHECK(snapshot().submissions == delivered);
  swap.images[0] = image0;
  reshade_test::Swapchain other = swap;
  present(nullptr, &other, nullptr, nullptr, 0, nullptr);
  CHECK(snapshot().submissions == delivered);
  std::thread wrong_thread([&] {
    present(nullptr, &swap, nullptr, nullptr, 0, nullptr);
  });
  wrong_thread.join();
  CHECK(snapshot().submissions == delivered);

  destroy(&swap, true);  // resize invalidates the old resource generation
  ComPtr<ID3D11Texture2D> resized;
  CHECK(MakeTexture(native.Get(), 32, 4, 0xff778899, resized));
  swap.images[0].handle = reinterpret_cast<uint64_t>(resized.Get());
  present(nullptr, &swap, nullptr, nullptr, 0, nullptr);
  s = snapshot();
  CHECK(s.submissions == delivered + 1 && s.readbacks == delivered + 1);
  CHECK(s.pixel == 0xff778899 && s.width == 32 && s.height == 4);
  CHECK(s.stamp.generation == 2 && s.captures == 1);
  destroy(&swap, false);
  other.images[0] = swap.images[0]; other.current = 0;
  present(nullptr, &other, nullptr, nullptr, 0, nullptr);
  s = snapshot();
  CHECK(s.submissions == delivered + 2 && s.stamp.generation == 3);
  CHECK(s.swapchain == reinterpret_cast<std::uintptr_t>(&other));
  CHECK(s.captures == 1 && s.polls == s.submissions);

  ComPtr<ID3D11Device> replacement;
  CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
      nullptr, 0, D3D11_SDK_VERSION, replacement.GetAddressOf(), nullptr, nullptr)));
  ComPtr<ID3D11Texture2D> foreign;
  CHECK(MakeTexture(replacement.Get(), 16, 8, 0xff000001, foreign));
  other.images[0].handle = reinterpret_cast<uint64_t>(foreign.Get());
  present(nullptr, &other, nullptr, nullptr, 0, nullptr);
  CHECK(snapshot().submissions == delivered + 2);
  other.images[0].handle = reinterpret_cast<uint64_t>(resized.Get());
  present(nullptr, &other, nullptr, nullptr, 0, nullptr);
  CHECK(snapshot().submissions == delivered + 2);  // permanent device refusal
  CHECK(FreeLibrary(fixture));
  CHECK(!GetModuleHandleW(L"DSVR.dll"));
  CHECK(DestroyWindow(window));
  std::printf("positive add-on/shared bridge: WARP texture/readback PASS; XR absent\n");
  return 0;
}

int wmain(int argc, wchar_t** argv) {
  CHECK(argc == 3 || argc == 4);
  HMODULE addon = LoadLibraryW(argv[1]);
  CHECK(addon);
  CHECK(!GetModuleHandleW(L"DSVR.dll"));  // add-on must not load/start the core
  using Init = BOOL(*)(HMODULE, HMODULE);
  using Uninit = void(*)(HMODULE, HMODULE);
  auto init = reinterpret_cast<Init>(GetProcAddress(addon, "AddonInit"));
  auto uninit = reinterpret_cast<Uninit>(GetProcAddress(addon, "AddonUninit"));
  CHECK(init && uninit);
  HMODULE host = GetModuleHandleW(nullptr);
  CHECK(!init(addon, host));  // unsupported API refuses without callbacks
  CHECK(registrations == 1 && events == 0);
  supported = true;
  CHECK(init(addon, host));
  CHECK(registrations == 2 && events == 2 && present && destroy);
  present(nullptr, nullptr, nullptr, nullptr, 0, nullptr);
  destroy(nullptr, false);
  MissingDeviceSwapchain missing;
  present(nullptr, &missing, nullptr, nullptr, 0, nullptr);
  destroy(&missing, true);
  CHECK(!GetModuleHandleW(L"DSVR.dll"));

  // Loading DSVR.dll is inert until DsvrCoreStart passes its game pin/config.
  // An arbitrary native pointer must be ignored by the unpublished bridge.
  HMODULE core = LoadLibraryW(argv[2]);
  CHECK(core);
  using Frame = void(__stdcall*)(std::uintptr_t, void*, void*);
  using Invalidate = void(__stdcall*)(std::uintptr_t);
  auto frame = reinterpret_cast<Frame>(GetProcAddress(core, "DsvrReShadeFrameV1"));
  auto invalidate = reinterpret_cast<Invalidate>(GetProcAddress(core, "DsvrReShadeInvalidateV1"));
  CHECK(frame && invalidate);
  frame(1, reinterpret_cast<void*>(1), reinterpret_cast<void*>(1));
  invalidate(1);
  CHECK(FreeLibrary(core));
  if (argc == 4) CHECK(PositiveFrame(argv[3]) == 0);
  uninit(addon, host);
  CHECK(events == 0 && !present && !destroy && unregistrations == 1);
  CHECK(FreeLibrary(addon));
  std::printf("add-on DLL boundary: PASS (registration/refusal/inert core only)\n");
  return 0;
}
