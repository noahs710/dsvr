#pragma once

#include <cstdint>

struct ID3D11Device;
struct ID3D11Texture2D;

namespace dsvr {

// Observation-only main-scene depth identification (reprojection stereo,
// phase 1). Hooks the captured immediate context's OMSetRenderTargets /
// Draw / DrawIndexed vtable entries and counts draws per bound
// (colour, depth) pair. It never changes state, never blocks, and never
// touches the depth contents: the only product is which native depth
// texture the main scene pass uses, proven by draw counts at runtime.
// A wrong guess costs nothing because nothing consumes the result yet.
bool DepthProbeInstall(ID3D11Device* device);
void DepthProbeUninstall();

// The currently leading main-scene candidate: the pair with the most
// draws among colour targets at least 1280x720. Returns false until a
// pair has drawn. Pointers are borrowed (no AddRef): callers must only
// compare identities or use them inside the same frame's submit.
bool DepthProbeMainPair(ID3D11Texture2D*& colour, ID3D11Texture2D*& depth,
                        std::uint32_t& colour_width, std::uint32_t& colour_height,
                        std::uint64_t& draws);

}  // namespace dsvr
