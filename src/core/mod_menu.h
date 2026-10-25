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
// select is the A/confirm button only; triggers belong to the spatial ray.
// prev_panel/next_panel are the left hand's X/Y while the menu captures input:
// the headset has no keyboard, so this is the only way to change screens.
void ModMenuControllerInput(float x, float y, bool select, bool back,
                            bool prev_panel = false, bool next_panel = false);
void ModMenuRenderTexture(ID3D11Device* device, ID3D11Texture2D* image);
void ModMenuShutdown();
using ModMenuPanel = void (*)();
// Register during startup. IDs/names are copied; duplicate IDs are refused.
bool ModMenuRegisterPanel(const char* id, const char* name, ModMenuPanel draw);
}  // namespace dsvr
