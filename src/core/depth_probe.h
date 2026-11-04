#pragma once

#include <cstdint>

struct ID3D11Device;
struct ID3D11Texture2D;

namespace dsvr {

// Observation-only D3D11 scene-pass discovery. The probe tracks the captured
// immediate context's render-target binds, draw families, clears, resource
// copies/resolves, command-list use and frame boundary. It never changes
// render state or reads resource contents. Reports distinguish hardware MSAA
// format support from MSAA actually used by a scene pass.
bool DepthProbeInstall(ID3D11Device* device);
void DepthProbeUninstall();

// Called once for each game image immediately before DSVR's own XR drawing.
// Flushes a bounded per-frame summary and resets frame counters. This does not
// imply the image was produced by the reported scene candidate.
void DepthProbeFrameBoundary(ID3D11Texture2D* presented_image,
                             std::uint64_t frame_ordinal);

// Borrowed until the next frame boundary. Prefers the color input bound at
// the final output, with observed-target ranking as discovery fallback.
// Returns null across observation gaps; the compositor retains its own copy.
ID3D11Texture2D* DepthProbeSceneSource();

// Keep DSVR's own menu and XR draws out of the game-scene observations.
void DepthProbeBeginInternalPass();
void DepthProbeEndInternalPass();

}  // namespace dsvr
