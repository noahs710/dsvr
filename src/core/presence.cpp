#include "presence.h"

#include <windows.h>
#include <MinHook.h>
#include <cstdint>
#include <atomic>

#include "../common/logging.h"
#include "../common/memory.h"
#include "diagnostics.h"
#include "hook_install.h"
#include "xr.h"

namespace dsvr {
namespace {

// Recovered from the pinned build's running-image analysis. The hook remains
// disabled unless the live prologue matches all 17 bytes.
constexpr std::uintptr_t kPlayerVisibilityRva = 0x3549e0;
constexpr unsigned char kPlayerVisibilityPrologue[] = {
    0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec,
    0x20, 0x80, 0xb9, 0xa6, 0x02, 0x00, 0x00, 0x80};
using PlayerVisibilityFn = void(__fastcall*)(void*);
PlayerVisibilityFn g_original = nullptr;
std::atomic<bool> g_enabled{false};
bool g_failure_logged = false;

bool IsLocalPlayer(void* player) {
  std::uintptr_t world = 0, local = 0;
  return player && GuardedRead(ModuleBase(nullptr) + 0x1c77e50, world) &&
         world && GuardedRead(world + 0x68, local) &&
         local == (std::uintptr_t)player;
}

bool HideModel(std::uintptr_t model, std::uintptr_t dirty_offset,
               std::uintptr_t visible_offset) {
  if (!model) return true;
  unsigned char visible = 0;
  if (!GuardedRead(model + visible_offset, visible)) return false;
  if (!(visible & 1)) return true;
  unsigned dirty = 0;
  if (!GuardedRead(model + dirty_offset, dirty)) return false;
  dirty |= 0x200;
  visible = (unsigned char)(visible & 0xfeu);
  return GuardedWriteBytes(model + dirty_offset, &dirty, sizeof(dirty)) &&
         GuardedWriteBytes(model + visible_offset, &visible, sizeof(visible));
}

bool g_policy_logged = false;

void __fastcall PlayerVisibilityDetour(void* player) {
  g_original(player);
  const Config cfg = DiagnosticsCurrentConfig();
  if (!g_enabled.load() || !cfg.vr_enabled || cfg.observer_only || !cfg.headlook_enabled ||
      !XrSessionRunning() || XrMenuPresentationActive() || !IsLocalPlayer(player)) return;
  std::uintptr_t regular = 0, alternate = 0;
  const bool read_ok = GuardedRead((std::uintptr_t)player + 0x60, regular) &&
                       GuardedRead((std::uintptr_t)player + 0x850, alternate);
  // Equipped players normally render only the alternate (+838 == 1). Hide it
  // as a duplicate only when native code also selected the primary model.
  unsigned char primary_visible = 0;
  const bool primary_shown =
      regular && GuardedRead(regular + 0x92, primary_visible) && (primary_visible & 1);
  // At most one of the two local-player render models may ever be hidden. The
  // live 23 Oct run had hide_player_model and hide_second_model both true and
  // the player was completely invisible for the whole session, so hiding the
  // primary is now an escape hatch for the headgear shell that keeps the body
  // model rendering instead of hiding everything.
  const bool hide_alternate = !cfg.hide_player_model && cfg.hide_second_model &&
      !cfg.forearm_only && regular && alternate && regular != alternate && primary_shown;
  bool write_ok = read_ok;
  if (cfg.hide_player_model) write_ok = write_ok && HideModel(regular, 0x84, 0x92);
  if (hide_alternate) write_ok = write_ok && HideModel(alternate, 0x9c, 0xaa);
  if (read_ok && !g_policy_logged) {
    g_policy_logged = true;
    DSVR_LOG("Presence: primary=%p alternate=%p hide_all=%d hide_alternate=%d forearm_only=%d",
        (void*)regular, (void*)alternate, (int)cfg.hide_player_model,
        (int)hide_alternate, (int)cfg.forearm_only);
    if (cfg.hide_player_model && cfg.hide_second_model)
      DSVR_WARN("Presence: hide_player_model and hide_second_model are both true; "
                "keeping the second model visible so the player is never invisible");
  }
  if (!write_ok && !g_failure_logged) {
    g_failure_logged = true;
    DSVR_WARN("Presence: guarded visibility write failed; native model retained");
  }
}

}  // namespace

bool PresenceHookInstall(bool vr_enabled) {
  std::lock_guard<std::mutex> hook_lock(HookInstallMutex());
  if (!vr_enabled) return false;
  const std::uintptr_t base = ModuleBase(nullptr);
  void* target = (void*)(base + kPlayerVisibilityRva);
  if (!BytesMatch((std::uintptr_t)target, kPlayerVisibilityPrologue,
                  sizeof(kPlayerVisibilityPrologue))) {
    DSVR_ERROR("Presence: signature mismatch at RVA 0x%llx; model unchanged",
               (unsigned long long)kPlayerVisibilityRva);
    return false;
  }
  MH_STATUS init = MH_Initialize();
  if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) return false;
  if (MH_CreateHook(target, (void*)&PlayerVisibilityDetour,
                    (void**)&g_original) != MH_OK) {
    DSVR_ERROR("Presence: visibility hook creation failed");
    return false;
  }
  if (MH_EnableHook(target) != MH_OK) {
    MH_RemoveHook(target);
    g_original = nullptr;
    DSVR_ERROR("Presence: visibility hook activation failed");
    return false;
  }
  g_enabled.store(true);
  DSVR_LOG("Presence: local-player model hiding ready (signature verified)");
  return true;
}

void PresenceHookUninstall() {
  std::lock_guard<std::mutex> hook_lock(HookInstallMutex());
  g_enabled.store(false);
  if(g_original)MH_DisableHook(reinterpret_cast<void*>(ModuleBase(nullptr)+kPlayerVisibilityRva));
  // Late calls retain their trampoline and pass native visibility through.
}

}  // namespace dsvr
