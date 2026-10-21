#include "depth_probe.h"

#include <windows.h>

#include <d3d11.h>
#include <MinHook.h>

#include <atomic>
#include <cstdint>
#include <cstring>

#include "../common/logging.h"

namespace dsvr {
namespace {

// ID3D11DeviceContext vtable order, verified against the Windows SDK
// d3d11.h ID3D11DeviceContextVtbl on this machine (10.0.26100.0):
// 12 DrawIndexed, 13 Draw, 33 OMSetRenderTargets. (An earlier 15/16/36
// guess hooked Unmap/PSSetConstantBuffers/OMSetDepthStencilState and
// crashed on the stencil-ref argument read as a view pointer.)
constexpr std::size_t kSlotDrawIndexed = 12;
constexpr std::size_t kSlotDraw = 13;
constexpr std::size_t kSlotOmSetRenderTargets = 33;

using DrawIndexedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, INT);
using DrawFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT);
using OmSetFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT,
                                         ID3D11RenderTargetView* const*,
                                         ID3D11DepthStencilView*);

DrawIndexedFn g_draw_indexed = nullptr;
DrawFn g_draw = nullptr;
OmSetFn g_om_set = nullptr;
void* g_targets[3] = {};

struct Pair {
  std::atomic<ID3D11Texture2D*> colour{nullptr};
  std::atomic<ID3D11Texture2D*> depth{nullptr};
  std::atomic<std::uint64_t> draws{0};
  std::atomic<std::uint32_t> width{0}, height{0};
};
Pair g_pairs[32];
thread_local ID3D11Texture2D* t_colour = nullptr;
thread_local ID3D11Texture2D* t_depth = nullptr;

// SEH-only helper (no C++ objects): a view whose implementation is not
// what the slot map expects must fail soft, never crash the game.
bool GuardedGetResource(ID3D11View* view, ID3D11Resource** out) {
  __try {
    view->GetResource(out);
    return *out != nullptr;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    *out = nullptr;
    return false;
  }
}

ID3D11Texture2D* ResourceOf(ID3D11View* view) {
  if (!view) return nullptr;
  ID3D11Resource* resource = nullptr;
  if (!GuardedGetResource(view, &resource)) return nullptr;
  ID3D11Texture2D* texture = nullptr;
  resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&texture);
  resource->Release();
  return texture;  // caller owns this reference
}

void CountDraw() {
  if (!t_colour) return;
  for (auto& pair : g_pairs) {
    if (pair.colour.load(std::memory_order_relaxed) == t_colour &&
        pair.depth.load(std::memory_order_relaxed) == t_depth) {
      pair.draws.fetch_add(1, std::memory_order_relaxed);
      return;
    }
  }
  for (auto& pair : g_pairs) {
    ID3D11Texture2D* expected = nullptr;
    if (pair.colour.compare_exchange_strong(expected, t_colour,
                                            std::memory_order_relaxed)) {
      pair.depth.store(t_depth, std::memory_order_relaxed);
      D3D11_TEXTURE2D_DESC desc{};
      t_colour->GetDesc(&desc);
      pair.width.store(desc.Width, std::memory_order_relaxed);
      pair.height.store(desc.Height, std::memory_order_relaxed);
      pair.draws.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    if (expected == t_colour &&
        pair.depth.load(std::memory_order_relaxed) == t_depth) {
      pair.draws.fetch_add(1, std::memory_order_relaxed);
      return;
    }
  }
}

void STDMETHODCALLTYPE OmSetDetour(ID3D11DeviceContext* context, UINT count,
                                   ID3D11RenderTargetView* const* rtvs,
                                   ID3D11DepthStencilView* dsv) {
  // Record before forwarding; identity only, references released at once.
  ID3D11Texture2D* colour = count && rtvs ? ResourceOf(rtvs[0]) : nullptr;
  ID3D11Texture2D* depth = ResourceOf(dsv);
  if (t_colour) t_colour->Release();
  if (t_depth) t_depth->Release();
  t_colour = colour;
  t_depth = depth;
  g_om_set(context, count, rtvs, dsv);
}

void STDMETHODCALLTYPE DrawIndexedDetour(ID3D11DeviceContext* context,
                                         UINT index_count, UINT start, INT base) {
  CountDraw();
  g_draw_indexed(context, index_count, start, base);
}

void STDMETHODCALLTYPE DrawDetour(ID3D11DeviceContext* context, UINT vertex_count,
                                  UINT start) {
  CountDraw();
  g_draw(context, vertex_count, start);
}

}  // namespace

bool DepthProbeInstall(ID3D11Device* device) {
  if (!device || g_targets[0]) return g_targets[0] != nullptr;
  ID3D11DeviceContext* context = nullptr;
  device->GetImmediateContext(&context);
  if (!context) return false;
  void** vtable = *reinterpret_cast<void***>(context);
  context->Release();
  void* wanted[3] = {vtable[kSlotDrawIndexed], vtable[kSlotDraw],
                     vtable[kSlotOmSetRenderTargets]};
  void* detours[3] = {(void*)&DrawIndexedDetour, (void*)&DrawDetour,
                      (void*)&OmSetDetour};
  void** originals[3] = {(void**)&g_draw_indexed, (void**)&g_draw,
                         (void**)&g_om_set};
  const MH_STATUS init = MH_Initialize();
  if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) return false;
  for (int i = 0; i < 3; ++i) {
    if (MH_CreateHook(wanted[i], detours[i], originals[i]) != MH_OK ||
        MH_EnableHook(wanted[i]) != MH_OK) {
      for (int j = 0; j < i; ++j) {
        MH_DisableHook(g_targets[j]);
        MH_RemoveHook(g_targets[j]);
        g_targets[j] = nullptr;
      }
      g_draw_indexed = nullptr;
      g_draw = nullptr;
      g_om_set = nullptr;
      DSVR_ERROR("DepthProbe: context vtable hook refused (slot %d)", i);
      return false;
    }
    g_targets[i] = wanted[i];
  }
  DSVR_LOG("DepthProbe: context draw observation installed (identification "
           "only; no state changed, no depth read)");
  return true;
}

void DepthProbeUninstall() {
  for (void*& target : g_targets) {
    if (target) MH_DisableHook(target);
    target = nullptr;
  }
  // Trampolines stay allocated: in-flight game draws may still call them.
}

bool DepthProbeMainPair(ID3D11Texture2D*& colour, ID3D11Texture2D*& depth,
                        std::uint32_t& colour_width, std::uint32_t& colour_height,
                        std::uint64_t& draws) {
  colour = nullptr;
  depth = nullptr;
  colour_width = colour_height = 0;
  draws = 0;
  for (auto& pair : g_pairs) {
    const auto count = pair.draws.load(std::memory_order_relaxed);
    const auto width = pair.width.load(std::memory_order_relaxed);
    if (!count || width < 1280 || pair.height.load(std::memory_order_relaxed) < 720)
      continue;
    if (count > draws) {
      draws = count;
      colour = pair.colour.load(std::memory_order_relaxed);
      depth = pair.depth.load(std::memory_order_relaxed);
      colour_width = width;
      colour_height = pair.height.load(std::memory_order_relaxed);
    }
  }
  return colour != nullptr;
}

}  // namespace dsvr
