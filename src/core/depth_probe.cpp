#include "depth_probe.h"

#include <windows.h>

#include <d3d11.h>
#include "d3d11_source_format.h"
#include <MinHook.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>

#include "../common/logging.h"

namespace dsvr {
namespace {

// These slots are from the Windows 10.0.26100.0 ID3D11DeviceContextVtbl.
// Begin, End and GetData precede SetPredication in this SDK, hence the +3
// offset from older hand-copied slot lists for methods after GSSetSamplers.
constexpr std::size_t kDrawIndexed = 12;
constexpr std::size_t kDraw = 13;
constexpr std::size_t kDrawIndexedInstanced = 20;
constexpr std::size_t kDrawInstanced = 21;
constexpr std::size_t kOmSetRenderTargets = 33;
constexpr std::size_t kOmSetRenderTargetsAndUavs = 34;
constexpr std::size_t kDrawAuto = 38;
constexpr std::size_t kDrawIndexedInstancedIndirect = 39;
constexpr std::size_t kDrawInstancedIndirect = 40;
constexpr std::size_t kCopySubresourceRegion = 46;
constexpr std::size_t kCopyResource = 47;
constexpr std::size_t kClearRenderTargetView = 50;
constexpr std::size_t kClearDepthStencilView = 53;
constexpr std::size_t kResolveSubresource = 57;
constexpr std::size_t kExecuteCommandList = 58;
constexpr std::size_t kClearState = 110;

constexpr UINT kKeepTargets = 0xffffffffu;
constexpr std::size_t kMaxPairs = 64;
constexpr std::size_t kMaxTransfers = 64;
constexpr std::size_t kMaxFormats = 32;
constexpr std::size_t kMaxTargets = D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;
constexpr std::size_t kLoggedTargets = 5;
constexpr std::size_t kLoggedTransfers = 8;

using DrawIndexedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, INT);
using DrawFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT);
using DrawIndexedInstancedFn = void(STDMETHODCALLTYPE*)(
    ID3D11DeviceContext*, UINT, UINT, UINT, INT, UINT);
using DrawInstancedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT,
                                                UINT, UINT);
using DrawAutoFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*);
using DrawIndirectFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,
                                                ID3D11Buffer*, UINT);
using OmSetFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT,
    ID3D11RenderTargetView* const*, ID3D11DepthStencilView*);
using OmSetUavsFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT,
    ID3D11RenderTargetView* const*, ID3D11DepthStencilView*, UINT, UINT,
    ID3D11UnorderedAccessView* const*, const UINT*);
using ClearRtvFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,
    ID3D11RenderTargetView*, const FLOAT[4]);
using ClearDsvFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,
    ID3D11DepthStencilView*, UINT, FLOAT, UINT8);
using CopyResourceFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,
    ID3D11Resource*, ID3D11Resource*);
using CopyRegionFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,
    ID3D11Resource*, UINT, UINT, UINT, UINT, ID3D11Resource*, UINT,
    const D3D11_BOX*);
using ResolveFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,
    ID3D11Resource*, UINT, ID3D11Resource*, UINT, DXGI_FORMAT);
using ExecuteCommandListFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,
    ID3D11CommandList*, BOOL);
using ClearStateFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*);

DrawIndexedFn g_draw_indexed = nullptr;
DrawFn g_draw = nullptr;
DrawIndexedInstancedFn g_draw_indexed_instanced = nullptr;
DrawInstancedFn g_draw_instanced = nullptr;
DrawAutoFn g_draw_auto = nullptr;
DrawIndirectFn g_draw_indexed_indirect = nullptr;
DrawIndirectFn g_draw_indirect = nullptr;
OmSetFn g_om_set = nullptr;
OmSetUavsFn g_om_set_uavs = nullptr;
ClearRtvFn g_clear_rtv = nullptr;
ClearDsvFn g_clear_dsv = nullptr;
CopyResourceFn g_copy_resource = nullptr;
CopyRegionFn g_copy_region = nullptr;
ResolveFn g_resolve = nullptr;
ExecuteCommandListFn g_execute_command_list = nullptr;
ClearStateFn g_clear_state = nullptr;

void* g_targets[16]{};
std::size_t g_hook_count = 0;
ID3D11Device* g_device = nullptr;  // lifetime owned by the render host
ID3D11DeviceContext* g_context = nullptr;  // probe holds GetImmediateContext ref
DWORD g_render_thread = 0;
std::atomic<bool> g_installed{false};
thread_local bool t_internal_pass = false;
std::atomic<std::uint64_t> g_unobserved_context_draws{0};
std::atomic<std::uint64_t> g_unobserved_thread_draws{0};

struct Target {
  ID3D11Texture2D* texture = nullptr;
  D3D11_TEXTURE2D_DESC resource{};
  DXGI_FORMAT view_format = DXGI_FORMAT_UNKNOWN;
  UINT view_dimension = 0;
};

struct Pair {
  ID3D11Texture2D* colour = nullptr;
  ID3D11Texture2D* depth = nullptr;
  D3D11_TEXTURE2D_DESC colour_desc{};
  D3D11_TEXTURE2D_DESC depth_desc{};
  DXGI_FORMAT colour_view_format = DXGI_FORMAT_UNKNOWN;
  DXGI_FORMAT depth_view_format = DXGI_FORMAT_UNKNOWN;
  UINT colour_view_dimension = 0;
  UINT depth_view_dimension = 0;
  std::uint64_t binds = 0;
  std::uint64_t draw = 0;
  std::uint64_t draw_indexed = 0;
  std::uint64_t draw_instanced = 0;
  std::uint64_t draw_indexed_instanced = 0;
  std::uint64_t draw_auto = 0;
  std::uint64_t draw_indirect = 0;
  std::uint64_t clear_colour = 0;
  std::uint64_t clear_depth = 0;
  std::uint64_t copies_in = 0;
  std::uint64_t copies_out = 0;
  std::uint64_t resolves_in = 0;
  std::uint64_t resolves_out = 0;
  std::uint64_t Draws() const {
    return draw + draw_indexed + draw_instanced + draw_indexed_instanced +
           draw_auto + draw_indirect;
  }
};

enum class TransferKind : std::uint32_t { Copy, CopyRegion, Resolve };
struct Transfer {
  ID3D11Texture2D* source = nullptr;
  ID3D11Texture2D* destination = nullptr;
  D3D11_TEXTURE2D_DESC source_desc{};
  D3D11_TEXTURE2D_DESC destination_desc{};
  TransferKind kind = TransferKind::Copy;
  DXGI_FORMAT resolve_format = DXGI_FORMAT_UNKNOWN;
  std::uint64_t count = 0;
};

struct FormatSupport {
  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
  UINT levels_2x = 0;
  UINT levels_4x = 0;
  HRESULT result_2x = E_FAIL;
  HRESULT result_4x = E_FAIL;
};

Pair g_pairs[kMaxPairs]{};
std::size_t g_pair_count = 0;
ID3D11Texture2D* g_scene_source = nullptr;
std::uint64_t g_scene_source_draws = 0;
bool g_scene_source_retained_after_miss = false;
Transfer g_transfers[kMaxTransfers]{};
std::size_t g_transfer_count = 0;
FormatSupport g_formats[kMaxFormats]{};
std::size_t g_format_count = 0;
std::uint64_t g_dropped_pairs = 0;
std::uint64_t g_dropped_transfers = 0;
std::uint64_t g_command_lists = 0;
std::uint64_t g_command_list_state_unknown = 0;
std::uint64_t g_frames = 0;
Target g_bound_colour[kMaxTargets]{};
Pair* g_bound_pairs[kMaxTargets]{};
std::size_t g_bound_colour_count = 0;
Target g_bound_depth{};

bool GuardedGetResource(ID3D11View* view, ID3D11Resource** out) {
  __try {
    view->GetResource(out);
    return *out != nullptr;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    *out = nullptr;
    return false;
  }
}

ID3D11Texture2D* TextureOf(ID3D11Resource* resource) {
  if (!resource) return nullptr;
  ID3D11Texture2D* texture = nullptr;
  if (FAILED(resource->QueryInterface(__uuidof(ID3D11Texture2D),
                                      reinterpret_cast<void**>(&texture))))
    return nullptr;
  return texture;
}

ID3D11Texture2D* TextureOf(ID3D11View* view) {
  if (!view) return nullptr;
  ID3D11Resource* resource = nullptr;
  if (!GuardedGetResource(view, &resource)) return nullptr;
  ID3D11Texture2D* texture = TextureOf(resource);
  resource->Release();
  return texture;
}

Target TargetOf(ID3D11RenderTargetView* view) {
  Target target{};
  if (!view) return target;
  target.texture = TextureOf(view);
  if (!target.texture) return target;
  target.texture->GetDesc(&target.resource);
  D3D11_RENDER_TARGET_VIEW_DESC desc{};
  view->GetDesc(&desc);
  target.view_format = desc.Format;
  target.view_dimension = static_cast<UINT>(desc.ViewDimension);
  return target;
}

Target TargetOf(ID3D11DepthStencilView* view) {
  Target target{};
  if (!view) return target;
  target.texture = TextureOf(view);
  if (!target.texture) return target;
  target.texture->GetDesc(&target.resource);
  D3D11_DEPTH_STENCIL_VIEW_DESC desc{};
  view->GetDesc(&desc);
  target.view_format = desc.Format;
  target.view_dimension = static_cast<UINT>(desc.ViewDimension);
  return target;
}

Target TargetOf(ID3D11Resource* resource) {
  Target target{};
  target.texture = TextureOf(resource);
  if (target.texture) {
    target.texture->GetDesc(&target.resource);
    target.view_format = target.resource.Format;
  }
  return target;
}

void ReleaseTarget(Target& target) {
  if (target.texture) target.texture->Release();
  target = {};
}

void ClearBindings() {
  for (std::size_t i = 0; i < kMaxTargets; ++i) {
    ReleaseTarget(g_bound_colour[i]);
    g_bound_pairs[i] = nullptr;
  }
  g_bound_colour_count = 0;
  ReleaseTarget(g_bound_depth);
}

Pair* FindPair(ID3D11Texture2D* colour, ID3D11Texture2D* depth,
               const Target* colour_target, const Target* depth_target) {
  for (std::size_t i = 0; i < g_pair_count; ++i) {
    auto& pair = g_pairs[i];
    if (pair.colour == colour && pair.depth == depth) {
      if (colour_target) {
        pair.colour_desc = colour_target->resource;
        pair.colour_view_format = colour_target->view_format;
        pair.colour_view_dimension = colour_target->view_dimension;
      }
      if (depth_target) {
        pair.depth_desc = depth_target->resource;
        pair.depth_view_format = depth_target->view_format;
        pair.depth_view_dimension = depth_target->view_dimension;
      }
      return &pair;
    }
  }
  if (g_pair_count == kMaxPairs) {
    ++g_dropped_pairs;
    return nullptr;
  }
  auto& pair = g_pairs[g_pair_count++];
  pair = {};
  pair.colour = colour;
  pair.depth = depth;
  if (pair.colour) pair.colour->AddRef();
  if (pair.depth) pair.depth->AddRef();
  if (colour_target) {
    pair.colour_desc = colour_target->resource;
    pair.colour_view_format = colour_target->view_format;
    pair.colour_view_dimension = colour_target->view_dimension;
  }
  if (depth_target) {
    pair.depth_desc = depth_target->resource;
    pair.depth_view_format = depth_target->view_format;
    pair.depth_view_dimension = depth_target->view_dimension;
  }
  return &pair;
}

void RecordBindings(bool count_bind = true) {
  for (std::size_t i = 0; i < g_bound_colour_count; ++i) {
    const auto& colour = g_bound_colour[i];
    auto* pair = FindPair(colour.texture, g_bound_depth.texture, &colour,
                          g_bound_depth.texture ? &g_bound_depth : nullptr);
    g_bound_pairs[i] = pair;
    if (pair && count_bind) ++pair->binds;
  }
  if (!g_bound_colour_count && g_bound_depth.texture) {
    auto* pair = FindPair(nullptr, g_bound_depth.texture, nullptr, &g_bound_depth);
    if (pair && count_bind) ++pair->binds;
  }
}

void SetBindings(UINT count, ID3D11RenderTargetView* const* rtvs,
                 ID3D11DepthStencilView* dsv) {
  ClearBindings();
  g_bound_colour_count = std::min<std::size_t>(count, kMaxTargets);
  for (std::size_t i = 0; i < g_bound_colour_count; ++i)
    g_bound_colour[i] = TargetOf(rtvs ? rtvs[i] : nullptr);
  g_bound_depth = TargetOf(dsv);
  RecordBindings();
}

void RecordDraw(unsigned kind) {
  for (std::size_t i = 0; i < g_bound_colour_count; ++i) {
    auto* pair = g_bound_pairs[i];
    if (!pair) continue;
    switch (kind) {
      case 0: ++pair->draw; break;
      case 1: ++pair->draw_indexed; break;
      case 2: ++pair->draw_instanced; break;
      case 3: ++pair->draw_indexed_instanced; break;
      case 4: ++pair->draw_auto; break;
      default: ++pair->draw_indirect; break;
    }
  }
}

void RecordClearColour(ID3D11RenderTargetView* view) {
  Target target = TargetOf(view);
  if (!target.texture) return;
  bool matched = false;
  for (std::size_t i = 0; i < g_bound_colour_count; ++i) {
    if (g_bound_colour[i].texture != target.texture) continue;
    auto* pair = g_bound_pairs[i];
    if (pair) ++pair->clear_colour;
    matched = true;
  }
  if (!matched) {
    auto* pair = FindPair(target.texture, nullptr, &target, nullptr);
    if (pair) ++pair->clear_colour;
  }
  ReleaseTarget(target);
}

void RecordClearDepth(ID3D11DepthStencilView* view) {
  Target target = TargetOf(view);
  if (!target.texture) return;
  if (g_bound_depth.texture == target.texture) {
    if (!g_bound_colour_count) {
      auto* pair = FindPair(nullptr, target.texture, nullptr, &target);
      if (pair) ++pair->clear_depth;
    }
    for (std::size_t i = 0; i < g_bound_colour_count; ++i) {
      auto* pair = g_bound_pairs[i];
      if (pair) ++pair->clear_depth;
    }
  } else {
    auto* pair = FindPair(nullptr, target.texture, nullptr, &target);
    if (pair) ++pair->clear_depth;
  }
  ReleaseTarget(target);
}

Transfer* FindTransfer(ID3D11Texture2D* source, ID3D11Texture2D* destination,
                       TransferKind kind) {
  for (std::size_t i = 0; i < g_transfer_count; ++i) {
    auto& transfer = g_transfers[i];
    if (transfer.source == source && transfer.destination == destination &&
        transfer.kind == kind)
      return &transfer;
  }
  if (g_transfer_count == kMaxTransfers) {
    ++g_dropped_transfers;
    return nullptr;
  }
  auto& transfer = g_transfers[g_transfer_count++];
  transfer = {};
  transfer.source = source;
  transfer.destination = destination;
  if (transfer.source) transfer.source->AddRef();
  if (transfer.destination) transfer.destination->AddRef();
  transfer.kind = kind;
  return &transfer;
}

void RecordTransfer(ID3D11Resource* destination, ID3D11Resource* source,
                    TransferKind kind, DXGI_FORMAT resolve_format) {
  Target src = TargetOf(source);
  Target dst = TargetOf(destination);
  if (!src.texture || !dst.texture) {
    ReleaseTarget(src);
    ReleaseTarget(dst);
    return;
  }
  auto* transfer = FindTransfer(src.texture, dst.texture, kind);
  if (transfer) {
    transfer->source_desc = src.resource;
    transfer->destination_desc = dst.resource;
    transfer->resolve_format = resolve_format;
    ++transfer->count;
  }
  for (std::size_t i = 0; i < g_pair_count; ++i) {
    auto& pair = g_pairs[i];
    if (pair.colour == src.texture) {
      if (kind == TransferKind::Resolve) ++pair.resolves_out;
      else ++pair.copies_out;
    }
    if (pair.colour == dst.texture) {
      if (kind == TransferKind::Resolve) ++pair.resolves_in;
      else ++pair.copies_in;
    }
  }
  ReleaseTarget(src);
  ReleaseTarget(dst);
}

std::uint64_t DrawCount(const Pair& pair) { return pair.Draws(); }

bool HasTransferPath(ID3D11Texture2D* start, ID3D11Texture2D* destination) {
  if (!start || !destination) return false;
  if (start == destination) return true;
  ID3D11Texture2D* visited[kMaxTransfers + 1]{};
  std::size_t visited_count = 1;
  visited[0] = start;
  for (std::size_t cursor = 0; cursor < visited_count; ++cursor) {
    for (std::size_t i = 0; i < g_transfer_count; ++i) {
      if (g_transfers[i].source != visited[cursor]) continue;
      auto* next = g_transfers[i].destination;
      if (next == destination) return true;
      bool seen = false;
      for (std::size_t j = 0; j < visited_count; ++j)
        if (visited[j] == next) seen = true;
      if (!seen && visited_count < std::size(visited))
        visited[visited_count++] = next;
    }
  }
  return false;
}

FormatSupport SupportFor(DXGI_FORMAT format) {
  for (std::size_t i = 0; i < g_format_count; ++i)
    if (g_formats[i].format == format) return g_formats[i];
  std::size_t slot = g_format_count;
  if (slot == kMaxFormats) slot = static_cast<std::size_t>(format) % kMaxFormats;
  else ++g_format_count;
  auto& support = g_formats[slot];
  support = {};
  support.format = format;
  if (!g_device || format == DXGI_FORMAT_UNKNOWN) return support;
  support.result_2x = g_device->CheckMultisampleQualityLevels(
      format, 2, &support.levels_2x);
  support.result_4x = g_device->CheckMultisampleQualityLevels(
      format, 4, &support.levels_4x);
  return support;
}

void ResetFrame() {
  for (std::size_t i = 0; i < g_pair_count; ++i) {
    if (g_pairs[i].colour) g_pairs[i].colour->Release();
    if (g_pairs[i].depth) g_pairs[i].depth->Release();
    g_pairs[i] = {};
  }
  for (std::size_t i = 0; i < g_transfer_count; ++i) {
    if (g_transfers[i].source) g_transfers[i].source->Release();
    if (g_transfers[i].destination) g_transfers[i].destination->Release();
    g_transfers[i] = {};
  }
  g_pair_count = 0;
  g_transfer_count = 0;
  g_dropped_pairs = 0;
  g_dropped_transfers = 0;
  g_command_lists = 0;
  g_command_list_state_unknown = 0;
}

void LogPair(const Pair& pair, ID3D11Texture2D* presented,
             std::uint64_t frame_ordinal) {
  const auto colour_support = SupportFor(pair.colour_view_format);
  const auto depth_support = SupportFor(pair.depth_view_format);
  const bool same_extent = pair.colour_desc.Width == pair.depth_desc.Width &&
      pair.colour_desc.Height == pair.depth_desc.Height;
  const bool matching_msaa = pair.depth && same_extent &&
      pair.colour_desc.SampleDesc.Count > 1 &&
      pair.colour_desc.SampleDesc.Count == pair.depth_desc.SampleDesc.Count;
  DSVR_LOG("DepthProbeTarget: frame=%llu colour=%p depth=%p %ux%u "
           "colour_fmt=%u view=%u samples=%u quality=%u depth_fmt=%u "
           "view=%u samples=%u quality=%u draws=%llu bind=%llu "
           "clear_c=%llu clear_d=%llu copy_in=%llu copy_out=%llu "
           "resolve_in=%llu resolve_out=%llu direct_present=%d "
           "copy_resolve_path_to_present=%d matching_msaa_candidate=%d "
           "colour_levels_2x=%u/4x=%u colour_check_hr=0x%08X/0x%08X "
           "depth_levels_2x=%u/4x=%u depth_check_hr=0x%08X/0x%08X",
           static_cast<unsigned long long>(frame_ordinal),
           static_cast<void*>(pair.colour), static_cast<void*>(pair.depth),
           pair.colour_desc.Width, pair.colour_desc.Height,
           static_cast<unsigned>(pair.colour_view_format),
           pair.colour_view_dimension, pair.colour_desc.SampleDesc.Count,
           pair.colour_desc.SampleDesc.Quality,
           static_cast<unsigned>(pair.depth_view_format),
           pair.depth_view_dimension, pair.depth_desc.SampleDesc.Count,
           pair.depth_desc.SampleDesc.Quality,
           static_cast<unsigned long long>(DrawCount(pair)),
           static_cast<unsigned long long>(pair.binds),
           static_cast<unsigned long long>(pair.clear_colour),
           static_cast<unsigned long long>(pair.clear_depth),
           static_cast<unsigned long long>(pair.copies_in),
           static_cast<unsigned long long>(pair.copies_out),
           static_cast<unsigned long long>(pair.resolves_in),
           static_cast<unsigned long long>(pair.resolves_out),
           pair.colour == presented ? 1 : 0,
           HasTransferPath(pair.colour, presented) ? 1 : 0,
           matching_msaa ? 1 : 0,
           colour_support.levels_2x, colour_support.levels_4x,
           static_cast<unsigned>(colour_support.result_2x),
           static_cast<unsigned>(colour_support.result_4x),
           depth_support.levels_2x, depth_support.levels_4x,
           static_cast<unsigned>(depth_support.result_2x),
           static_cast<unsigned>(depth_support.result_4x));
}

void FrameBoundary(ID3D11Texture2D* presented, std::uint64_t frame_ordinal) {
  if (!g_installed.load(std::memory_order_acquire)) return;
  Pair* scene_candidate = nullptr;
  Target final_input{};
  ID3D11RenderTargetView* final_output = nullptr;
  g_context->OMGetRenderTargets(1, &final_output, nullptr);
  const Target output = TargetOf(final_output);
  if (final_output) final_output->Release();
  if (presented && output.texture == presented) {
    ID3D11ShaderResourceView* input = nullptr;
    g_context->PSGetShaderResources(0, 1, &input);
    if (input) {
      final_input.texture = TextureOf(input);
      if (final_input.texture) final_input.texture->GetDesc(&final_input.resource);
      input->Release();
    }
  }
  Target output_ref = output;
  ReleaseTarget(output_ref);
  std::uint64_t scene_draws = 0;
  for (std::size_t i = 0; i < g_pair_count; ++i) {
    auto& pair = g_pairs[i];
    if (!pair.colour || pair.colour == presented || pair.depth ||
        pair.colour_desc.Width < 64 || pair.colour_desc.Height < 64 ||
        !IsDisplayColorFormat(pair.colour_desc.Format))
      continue;
    const auto draws = DrawCount(pair);
    if (draws <= scene_draws) continue;
    scene_candidate = &pair;
    scene_draws = draws;
  }
  const bool final_input_valid = final_input.texture && final_input.texture != presented &&
      final_input.resource.ArraySize == 1 &&
      final_input.resource.Width >= 64 && final_input.resource.Height >= 64 &&
      (final_input.resource.BindFlags & D3D11_BIND_RENDER_TARGET) &&
      IsDisplayColorFormat(final_input.resource.Format);
  auto* selected_source = final_input_valid ? final_input.texture
      : scene_candidate ? scene_candidate->colour : nullptr;
  if (selected_source) {
    if (g_scene_source != selected_source) {
      if (g_scene_source) g_scene_source->Release();
      g_scene_source = selected_source;
      g_scene_source->AddRef();
      DSVR_LOG("DepthProbe: source evidence=%s", final_input_valid
          ? "final-output-bound-input0" : "observed-target-candidate");
    }
    g_scene_source_draws = scene_draws;
    g_scene_source_retained_after_miss = false;
  } else if (g_scene_source) {
    // Some game frames bypass the observed immediate-context draw hooks.
    // Keep the last known rendered surface instead of sampling the game's
    // permanently black host-present backbuffer for those frames.
    if (!g_scene_source_retained_after_miss) {
      DSVR_WARN("DepthProbe: no scene target observed at frame boundary; "
                "compositor retains its completed source copy");
      g_scene_source_retained_after_miss = true;
    }
  }
  ReleaseTarget(final_input);
  D3D11_TEXTURE2D_DESC desc{};
  if (presented) presented->GetDesc(&desc);
  const auto other_context_draws =
      g_unobserved_context_draws.exchange(0, std::memory_order_relaxed);
  const auto other_thread_draws =
      g_unobserved_thread_draws.exchange(0, std::memory_order_relaxed);
  ++g_frames;
  const bool report = g_frames <= 3 || g_frames % 300 == 0;
  if (report) {
    DSVR_LOG("DepthProbeFrame: ordinal=%llu presented=%p %ux%u fmt=%u "
             "samples=%u targets=%llu transfers=%llu deferred_or_other_draws=%llu "
             "other_thread_draws=%llu command_lists=%llu unknown_list_state=%llu "
             "dropped_targets=%llu dropped_transfers=%llu "
             "scene_jitter_hook=0 taa_reprojection_inputs=unverified",
             static_cast<unsigned long long>(frame_ordinal),
             static_cast<void*>(presented), desc.Width, desc.Height,
             static_cast<unsigned>(desc.Format), desc.SampleDesc.Count,
             static_cast<unsigned long long>(g_pair_count),
             static_cast<unsigned long long>(g_transfer_count),
             static_cast<unsigned long long>(other_context_draws),
             static_cast<unsigned long long>(other_thread_draws),
             static_cast<unsigned long long>(g_command_lists),
             static_cast<unsigned long long>(g_command_list_state_unknown),
             static_cast<unsigned long long>(g_dropped_pairs),
             static_cast<unsigned long long>(g_dropped_transfers));
    std::size_t selected[kLoggedTargets]{};
    std::size_t selected_count = 0;
    for (std::size_t i = 0; i < g_pair_count; ++i) {
      const auto& candidate = g_pairs[i];
      if (!candidate.colour || candidate.colour_desc.Width < 1280 ||
          candidate.colour_desc.Height < 720 || !DrawCount(candidate))
        continue;
      std::size_t at = selected_count;
      while (at > 0 && DrawCount(g_pairs[selected[at - 1]]) < DrawCount(candidate))
        --at;
      if (at >= kLoggedTargets) continue;
      if (selected_count < kLoggedTargets) ++selected_count;
      for (std::size_t move = selected_count - 1; move > at; --move)
        selected[move] = selected[move - 1];
      selected[at] = i;
    }
    for (std::size_t i = 0; i < selected_count; ++i)
      LogPair(g_pairs[selected[i]], presented, frame_ordinal);
    const auto logged_transfers = std::min(g_transfer_count, kLoggedTransfers);
    for (std::size_t i = 0; i < logged_transfers; ++i) {
      const auto& transfer = g_transfers[i];
      const char* name = transfer.kind == TransferKind::Resolve ? "resolve" :
          transfer.kind == TransferKind::CopyRegion ? "copy-region" : "copy";
      DSVR_LOG("DepthProbeTransfer: frame=%llu op=%s src=%p %ux%u fmt=%u "
               "samples=%u dst=%p %ux%u fmt=%u samples=%u resolve_fmt=%u "
               "count=%llu dst_is_present=%d",
               static_cast<unsigned long long>(frame_ordinal), name,
               static_cast<void*>(transfer.source),
               transfer.source_desc.Width, transfer.source_desc.Height,
               static_cast<unsigned>(transfer.source_desc.Format),
               transfer.source_desc.SampleDesc.Count,
               static_cast<void*>(transfer.destination),
               transfer.destination_desc.Width, transfer.destination_desc.Height,
               static_cast<unsigned>(transfer.destination_desc.Format),
               transfer.destination_desc.SampleDesc.Count,
               static_cast<unsigned>(transfer.resolve_format),
               static_cast<unsigned long long>(transfer.count),
               transfer.destination == presented ? 1 : 0);
    }
  }
  ResetFrame();
  // D3D11 keeps output bindings across Present. Seed the next frame's target
  // pair so draws still attach to it even if the game does not rebind first.
  RecordBindings(false);
}

bool IsObserved(ID3D11DeviceContext* context) {
  if (!g_installed.load(std::memory_order_acquire) || t_internal_pass)
    return false;
  if (context != g_context) return false;
  return GetCurrentThreadId() == g_render_thread;
}

void NoteUnobservedDraw(ID3D11DeviceContext* context) {
  if (!g_installed.load(std::memory_order_acquire) || t_internal_pass) return;
  if (context != g_context)
    g_unobserved_context_draws.fetch_add(1, std::memory_order_relaxed);
  else if (GetCurrentThreadId() != g_render_thread)
    g_unobserved_thread_draws.fetch_add(1, std::memory_order_relaxed);
}

void STDMETHODCALLTYPE DrawIndexedDetour(ID3D11DeviceContext* context,
                                         UINT count, UINT start, INT base) {
  if (IsObserved(context)) RecordDraw(1); else NoteUnobservedDraw(context);
  g_draw_indexed(context, count, start, base);
}
void STDMETHODCALLTYPE DrawDetour(ID3D11DeviceContext* context, UINT count,
                                  UINT start) {
  if (IsObserved(context)) RecordDraw(0); else NoteUnobservedDraw(context);
  g_draw(context, count, start);
}
void STDMETHODCALLTYPE DrawIndexedInstancedDetour(ID3D11DeviceContext* context,
    UINT index_count, UINT instance_count, UINT start_index, INT base_vertex,
    UINT start_instance) {
  if (IsObserved(context)) RecordDraw(3); else NoteUnobservedDraw(context);
  g_draw_indexed_instanced(context, index_count, instance_count, start_index,
                           base_vertex, start_instance);
}
void STDMETHODCALLTYPE DrawInstancedDetour(ID3D11DeviceContext* context,
    UINT vertex_count, UINT instance_count, UINT start_vertex,
    UINT start_instance) {
  if (IsObserved(context)) RecordDraw(2); else NoteUnobservedDraw(context);
  g_draw_instanced(context, vertex_count, instance_count, start_vertex,
                   start_instance);
}
void STDMETHODCALLTYPE DrawAutoDetour(ID3D11DeviceContext* context) {
  if (IsObserved(context)) RecordDraw(4); else NoteUnobservedDraw(context);
  g_draw_auto(context);
}
void STDMETHODCALLTYPE DrawIndexedIndirectDetour(ID3D11DeviceContext* context,
    ID3D11Buffer* buffer, UINT offset) {
  if (IsObserved(context)) RecordDraw(5); else NoteUnobservedDraw(context);
  g_draw_indexed_indirect(context, buffer, offset);
}
void STDMETHODCALLTYPE DrawIndirectDetour(ID3D11DeviceContext* context,
    ID3D11Buffer* buffer, UINT offset) {
  if (IsObserved(context)) RecordDraw(5); else NoteUnobservedDraw(context);
  g_draw_indirect(context, buffer, offset);
}

void STDMETHODCALLTYPE OmSetDetour(ID3D11DeviceContext* context, UINT count,
    ID3D11RenderTargetView* const* rtvs, ID3D11DepthStencilView* dsv) {
  g_om_set(context, count, rtvs, dsv);
  if (IsObserved(context)) SetBindings(count, rtvs, dsv);
}
void STDMETHODCALLTYPE OmSetUavsDetour(ID3D11DeviceContext* context, UINT count,
    ID3D11RenderTargetView* const* rtvs, ID3D11DepthStencilView* dsv,
    UINT uav_start, UINT uav_count, ID3D11UnorderedAccessView* const* uavs,
    const UINT* initial_counts) {
  g_om_set_uavs(context, count, rtvs, dsv, uav_start, uav_count, uavs,
                initial_counts);
  if (IsObserved(context) && count != kKeepTargets)
    SetBindings(count, rtvs, dsv);
}
void STDMETHODCALLTYPE ClearRtvDetour(ID3D11DeviceContext* context,
    ID3D11RenderTargetView* view, const FLOAT colour[4]) {
  g_clear_rtv(context, view, colour);
  if (IsObserved(context)) RecordClearColour(view);
}
void STDMETHODCALLTYPE ClearDsvDetour(ID3D11DeviceContext* context,
    ID3D11DepthStencilView* view, UINT flags, FLOAT depth, UINT8 stencil) {
  g_clear_dsv(context, view, flags, depth, stencil);
  if (IsObserved(context)) RecordClearDepth(view);
}
void STDMETHODCALLTYPE CopyResourceDetour(ID3D11DeviceContext* context,
    ID3D11Resource* destination, ID3D11Resource* source) {
  g_copy_resource(context, destination, source);
  if (IsObserved(context))
    RecordTransfer(destination, source, TransferKind::Copy,
                   DXGI_FORMAT_UNKNOWN);
}
void STDMETHODCALLTYPE CopyRegionDetour(ID3D11DeviceContext* context,
    ID3D11Resource* destination, UINT dst_subresource, UINT dst_x, UINT dst_y,
    UINT dst_z, ID3D11Resource* source, UINT src_subresource,
    const D3D11_BOX* source_box) {
  g_copy_region(context, destination, dst_subresource, dst_x, dst_y, dst_z,
                source, src_subresource, source_box);
  if (IsObserved(context))
    RecordTransfer(destination, source, TransferKind::CopyRegion,
                   DXGI_FORMAT_UNKNOWN);
}
void STDMETHODCALLTYPE ResolveDetour(ID3D11DeviceContext* context,
    ID3D11Resource* destination, UINT dst_subresource, ID3D11Resource* source,
    UINT src_subresource, DXGI_FORMAT format) {
  g_resolve(context, destination, dst_subresource, source, src_subresource,
            format);
  if (IsObserved(context))
    RecordTransfer(destination, source, TransferKind::Resolve, format);
}
void STDMETHODCALLTYPE ExecuteCommandListDetour(ID3D11DeviceContext* context,
    ID3D11CommandList* command_list, BOOL restore_state) {
  g_execute_command_list(context, command_list, restore_state);
  if (IsObserved(context)) {
    ++g_command_lists;
    if (!restore_state) {
      ++g_command_list_state_unknown;
      ClearBindings();
    }
  }
}
void STDMETHODCALLTYPE ClearStateDetour(ID3D11DeviceContext* context) {
  g_clear_state(context);
  if (IsObserved(context)) ClearBindings();
}

struct HookSpec {
  std::size_t slot;
  void* detour;
  void** original;
  const char* name;
};

}  // namespace

bool DepthProbeInstall(ID3D11Device* device) {
  if (!device) return false;
  if (g_installed.load(std::memory_order_acquire)) return device == g_device;
  ID3D11DeviceContext* context = nullptr;
  device->GetImmediateContext(&context);
  if (!context) return false;
  void** vtable = *reinterpret_cast<void***>(context);
  HookSpec hooks[] = {
      {kDrawIndexed, (void*)&DrawIndexedDetour, (void**)&g_draw_indexed, "DrawIndexed"},
      {kDraw, (void*)&DrawDetour, (void**)&g_draw, "Draw"},
      {kDrawIndexedInstanced, (void*)&DrawIndexedInstancedDetour,
       (void**)&g_draw_indexed_instanced, "DrawIndexedInstanced"},
      {kDrawInstanced, (void*)&DrawInstancedDetour,
       (void**)&g_draw_instanced, "DrawInstanced"},
      {kDrawAuto, (void*)&DrawAutoDetour, (void**)&g_draw_auto, "DrawAuto"},
      {kDrawIndexedInstancedIndirect, (void*)&DrawIndexedIndirectDetour,
       (void**)&g_draw_indexed_indirect, "DrawIndexedInstancedIndirect"},
      {kDrawInstancedIndirect, (void*)&DrawIndirectDetour,
       (void**)&g_draw_indirect, "DrawInstancedIndirect"},
      {kOmSetRenderTargets, (void*)&OmSetDetour, (void**)&g_om_set,
       "OMSetRenderTargets"},
      {kOmSetRenderTargetsAndUavs, (void*)&OmSetUavsDetour,
       (void**)&g_om_set_uavs, "OMSetRenderTargetsAndUnorderedAccessViews"},
      {kClearRenderTargetView, (void*)&ClearRtvDetour, (void**)&g_clear_rtv,
       "ClearRenderTargetView"},
      {kClearDepthStencilView, (void*)&ClearDsvDetour, (void**)&g_clear_dsv,
       "ClearDepthStencilView"},
      {kCopyResource, (void*)&CopyResourceDetour, (void**)&g_copy_resource,
       "CopyResource"},
      {kCopySubresourceRegion, (void*)&CopyRegionDetour,
       (void**)&g_copy_region, "CopySubresourceRegion"},
      {kResolveSubresource, (void*)&ResolveDetour, (void**)&g_resolve,
       "ResolveSubresource"},
      {kExecuteCommandList, (void*)&ExecuteCommandListDetour,
       (void**)&g_execute_command_list, "ExecuteCommandList"},
      {kClearState, (void*)&ClearStateDetour, (void**)&g_clear_state,
       "ClearState"},
  };
  const auto init = MH_Initialize();
  if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
    context->Release();
    DSVR_ERROR("DepthProbe: MinHook initialization refused (%d)", init);
    return false;
  }
  for (std::size_t i = 0; i < std::size(hooks); ++i) {
    void* target = vtable[hooks[i].slot];
    bool duplicate = false;
    for (std::size_t j = 0; j < i; ++j)
      duplicate |= vtable[hooks[j].slot] == target;
    if (duplicate || MH_CreateHook(target, hooks[i].detour,
                                   hooks[i].original) != MH_OK) {
      for (std::size_t j = 0; j < g_hook_count; ++j) {
        MH_DisableHook(g_targets[j]);
        MH_RemoveHook(g_targets[j]);
        g_targets[j] = nullptr;
      }
      g_hook_count = 0;
      context->Release();
      DSVR_ERROR("DepthProbe: hook installation refused at %s (slot %llu)%s",
          hooks[i].name, static_cast<unsigned long long>(hooks[i].slot),
          duplicate ? "; SDK method addresses were aliased" : "");
      return false;
    }
    g_targets[g_hook_count++] = target;
    if (MH_EnableHook(target) != MH_OK) {
      for (std::size_t j = 0; j < g_hook_count; ++j) {
        MH_DisableHook(g_targets[j]);
        MH_RemoveHook(g_targets[j]);
        g_targets[j] = nullptr;
      }
      g_hook_count = 0;
      context->Release();
      DSVR_ERROR("DepthProbe: hook enable refused at %s (slot %llu)",
          hooks[i].name, static_cast<unsigned long long>(hooks[i].slot));
      return false;
    }
  }
  g_device = device;
  g_context = context;
  g_render_thread = GetCurrentThreadId();
  g_installed.store(true, std::memory_order_release);
  DSVR_LOG("DepthProbe: observation hooks installed on immediate context; "
           "render state unchanged; slots=%llu",
           static_cast<unsigned long long>(g_hook_count));
  return true;
}

void DepthProbeUninstall() {
  if (!g_installed.load(std::memory_order_acquire)) return;
  for (std::size_t i = 0; i < g_hook_count; ++i)
    if (g_targets[i]) MH_DisableHook(g_targets[i]);
  g_installed.store(false, std::memory_order_release);
  ClearBindings();
  ResetFrame();
  if (g_scene_source) g_scene_source->Release();
  g_scene_source = nullptr;
  g_scene_source_draws = 0;
  g_scene_source_retained_after_miss = false;
  if (g_context) g_context->Release();
  g_context = nullptr;
  g_device = nullptr;
  // MinHook trampolines remain allocated for any in-flight detour.
}

void DepthProbeFrameBoundary(ID3D11Texture2D* presented_image,
                             std::uint64_t frame_ordinal) {
  FrameBoundary(presented_image, frame_ordinal);
}

ID3D11Texture2D* DepthProbeSceneSource() {
  static ID3D11Texture2D* last_logged = nullptr;
  if (g_scene_source && g_scene_source != last_logged) {
    D3D11_TEXTURE2D_DESC desc{};
    g_scene_source->GetDesc(&desc);
    DSVR_LOG("DepthProbe: selected scene source %p %ux%u fmt=%u draws=%llu",
             static_cast<void*>(g_scene_source), desc.Width, desc.Height,
             static_cast<unsigned>(desc.Format),
             static_cast<unsigned long long>(g_scene_source_draws));
    last_logged = g_scene_source;
  }
  return g_scene_source_retained_after_miss ? nullptr : g_scene_source;
}

void DepthProbeBeginInternalPass() { t_internal_pass = true; }
void DepthProbeEndInternalPass() {
  t_internal_pass = false;
  // DSVR may leave a different RTV bound from the game's scene target. Forget
  // the old observation so the next frame starts only after a game rebind.
  if (g_installed.load(std::memory_order_acquire) &&
      GetCurrentThreadId() == g_render_thread)
  {
    ID3D11RenderTargetView* targets[kMaxTargets]{};
    ID3D11DepthStencilView* depth = nullptr;
    g_context->OMGetRenderTargets(kMaxTargets, targets, &depth);
    UINT count = 0;
    for (UINT i = 0; i < kMaxTargets; ++i) if (targets[i]) count = i + 1;
    SetBindings(count, targets, depth);
    for (auto* target : targets) if (target) target->Release();
    if (depth) depth->Release();
  }
}

}  // namespace dsvr
