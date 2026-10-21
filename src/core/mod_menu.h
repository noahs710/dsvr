#pragma once

struct ID3D11Device;
struct ID3D11Texture2D;

namespace dsvr {
// All GUI/D3D work is confined to the accepted host's render thread.
// Commands/input publication are safe from diagnostic and XR input threads.
void ModMenuToggle();
void ModMenuClose();
bool ModMenuVisible();
bool ModMenuCapturesInput();
void ModMenuControllerInput(float x, float y, bool select, bool back);
void ModMenuRenderTexture(ID3D11Device* device, ID3D11Texture2D* image);
void ModMenuShutdown();
using ModMenuPanel = void (*)();
// Register during startup. IDs/names are copied; duplicate IDs are refused.
bool ModMenuRegisterPanel(const char* id, const char* name, ModMenuPanel draw);
}  // namespace dsvr
