#include "native_ui.h"
#include <windows.h>
#include <MinHook.h>
#include <atomic>
#include "../common/memory.h"
#include "camera.h"
#include "diagnostics.h"
#include "hook_install.h"
#include "mod_menu.h"
#include "ui_suppression.h"
#include "xr.h"
namespace dsvr {
namespace {
// Frame-2774 HUD sprite/font draws enter 1e5b70 through 1e9958 (return
// address 1e9af6). Corpus batch-000246: this function sets render context,
// executes the 2D drawing command buffer, then restores its read cursor.
// No game tick, input, animation or menu navigation is dispatched here.
constexpr std::uintptr_t kDraw=0x1e5b70;
constexpr unsigned char kBytes[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,
    0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x20};
using Draw=void(__fastcall*)(void*,std::uintptr_t,void*,void*);
Draw g_original=nullptr;
std::atomic<bool> g_enabled{false};
std::atomic<std::uint64_t> g_visits{0},g_hidden{0};
void __fastcall DrawCommands(void* commands,std::uintptr_t scene,void* context,void* pass) {
  ++g_visits;
  const auto config=DiagnosticsCurrentConfig();
  UiSuppression policy;
  if(g_enabled.load() && config.vr_enabled && !config.observer_only &&
      XrSessionRunning() && CameraGameplayReady() && !ModMenuVisible() &&
      policy.Update(config.hide_ui_in_vr,XrMenuPresentationActive()?UiPresentation::Theater:UiPresentation::Immersive,true)) {
    ++g_hidden;return;
  }
  g_original(commands,scene,context,pass);
}
}
bool NativeUiHookInstall(bool enabled) {
  if(!enabled)return false;
  std::lock_guard<std::mutex> lock(HookInstallMutex());
  const auto target=ModuleBase(nullptr)+kDraw;
  if(!BytesMatch(target,kBytes,sizeof(kBytes)))return false;
  const auto initialized=MH_Initialize();
  if(initialized!=MH_OK && initialized!=MH_ERROR_ALREADY_INITIALIZED)return false;
  if(!g_original && MH_CreateHook(reinterpret_cast<void*>(target),reinterpret_cast<void*>(DrawCommands),
      reinterpret_cast<void**>(&g_original))!=MH_OK)return false;
  const auto result=MH_EnableHook(reinterpret_cast<void*>(target));
  g_enabled.store(result==MH_OK || result==MH_ERROR_ENABLED);
  return g_enabled.load();
}
void NativeUiHookUninstall() {
  g_enabled.store(false);
  std::lock_guard<std::mutex> lock(HookInstallMutex());
  if(g_original)MH_DisableHook(reinterpret_cast<void*>(ModuleBase(nullptr)+kDraw));
  // Keep the trampoline alive for callbacks already in flight.
}
NativeUiStatus NativeUiGetStatus() {return {g_enabled.load(),g_visits.load(),g_hidden.load()};}
}
