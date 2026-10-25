#include "body_visibility.h"

#include <windows.h>
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>

#include "../common/memory.h"
#include "diagnostics.h"
#include "hook_install.h"
#include "xr.h"

namespace dsvr {
namespace {
// Pinned native corpus: 206590 maps face/head/body/arms/legs/hair into
// slots 1..6; slot 0 is the generated face supplied by the actor's FgModel.
// 213900 sends dirty part records through 219230 (opcode 8059).
// 2192a0 copies these records; 2162d0/217370/217970/217dc0 draw only low-nibble
// types 1 or 2. Clearing the type excludes geometry without stretching skin.
constexpr std::uintptr_t kQueueParts = 0x219230;
constexpr std::uintptr_t kModelUpdate = 0x213900;
constexpr unsigned char kQueueBytes[]{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,
    0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20};
constexpr unsigned char kUpdateBytes[]{0x4c,0x8b,0xdc,0x53,0x56,0x48,0x81,
    0xec,0x88,0,0,0,0x0f,0x29,0x7c,0x24,0x60};
constexpr unsigned kPartCount = 13;
constexpr std::uint32_t kPartsDirty = 1u << 18;
using Parts = std::array<unsigned char, kPartCount * 16>;
using QueueParts = void(__fastcall*)(void*, std::uint32_t, const void*, int);
// The real update preserves XMM1 and passes it as XMM2 to 219d10(queue,id,dt).
using ModelUpdate = void(__fastcall*)(void*, float);
QueueParts g_original_queue = nullptr;
ModelUpdate g_original_update = nullptr;
std::atomic<bool> g_enabled{false};
std::atomic<std::uint64_t> g_visits{0}, g_filtered{0}, g_refused{0}, g_refreshes{0};

struct Binding {
  std::uintptr_t player = 0, primary = 0, alternate = 0, flver = 0, parts = 0;
  std::uintptr_t facegen_owner = 0, generated_face = 0;
  std::uint32_t id = 0;
  bool operator==(const Binding&) const = default;
};
struct AppliedParts {
  Binding binding{};
  bool valid = false;
  unsigned mask = 0;
};
std::mutex g_applied_mutex;
AppliedParts g_applied;

bool Type(std::uintptr_t object, std::uintptr_t base, std::uintptr_t rva) {
  std::uintptr_t table = 0;
  return object && GuardedRead(object, table) && table == base + rva;
}

bool ReadBinding(Binding& out) {
  const auto base = ModuleBase(nullptr);
  std::uintptr_t world = 0, alternate_flver = 0;
  int count = 0;
  short native_counts[2]{};
  Binding result{};
  if (!base || !GuardedRead(base + 0x1c77e50, world) || !world ||
      !GuardedRead(world + 0x68, result.player) ||
      !Type(result.player, base, 0x13251f0) ||
      !GuardedRead(result.player + 0x60, result.primary) ||
      !Type(result.primary, base, 0x12eaf30) ||
      !GuardedRead(result.player + 0x850, result.alternate) ||
      !Type(result.alternate, base, 0x12eace8) ||
      !GuardedRead(result.primary + 0x18, result.flver) ||
      !Type(result.flver, base, 0x12eb760) ||
      !GuardedRead(result.alternate + 0x18, alternate_flver) ||
      alternate_flver != result.flver ||
      !GuardedReadBytes(result.flver + 0x10, native_counts, sizeof(native_counts)) ||
      native_counts[0] != 69 || native_counts[1] != 80 ||
      !GuardedRead(result.alternate + 0x10, result.id) ||
      !GuardedRead(result.alternate + 0x78, count) || count != kPartCount ||
      !GuardedRead(result.alternate + 0x80, result.parts) || !result.parts) return false;
  // 357ae0 supplies actor+7d8 FgModel+28 to ALT+1c0 through 207dd0;
  // 2065f0 then passes that resource to 211610 for slot 0. It includes eyes.
  // Qualify this optional layer independently: an unknown base resource must
  // remain visible while the established helmet/face/hair filter still works.
  std::uintptr_t fg = 0, face = 0, alternate_face = 0;
  if (GuardedRead(result.player + 0x7d8, fg) && Type(fg, base, 0x13df6d0) &&
      GuardedRead(fg + 0x28, face) && face &&
      GuardedRead(result.alternate + 0x1c0, alternate_face) && alternate_face == face) {
    result.facegen_owner = fg;
    result.generated_face = face;
  }
  out = result;
  return true;
}

// Part records that lose their low type nibble. Bits are native part slots in
// the pinned 206590 corpus: 0 is the FgModel-generated face, 1 face, 2 head
// gear, 3 body, 4 arms, 5 legs, 6 hair. Clearing a type excludes geometry
// without stretching skin, and the primary (weapon) model is never read here,
// so a torso/leg filter keeps arms and weapons drawing.
unsigned FilterMask() {
  const auto config = DiagnosticsCurrentConfig();
  if (!g_enabled.load() || !config.vr_enabled || config.observer_only ||
      !config.headlook_enabled || !XrSessionRunning() ||
      XrMenuPresentationActive())
    return 0;
  unsigned mask = 0;
  if (config.hide_head) mask |= (1u << 0) | (1u << 1) | (1u << 2) | (1u << 6);
  if (config.hide_torso_and_legs) mask |= (1u << 3) | (1u << 5);
  return mask;
}

bool IsLocalAlternate(std::uintptr_t model) {
  const auto base = ModuleBase(nullptr);
  std::uintptr_t world = 0, player = 0, alternate = 0;
  return base && GuardedRead(base + 0x1c77e50, world) && world &&
      GuardedRead(world + 0x68, player) && player &&
      GuardedRead(player + 0x850, alternate) && alternate == model;
}

void Remember(const Binding& binding, unsigned mask) {
  std::lock_guard<std::mutex> lock(g_applied_mutex);
  g_applied = {binding, true, mask};
}

// A policy transition needs a fresh native parts command, even when equipment
// did not change. This is the native updater's own bounded dirty flag; all
// source part pointers, flags, palettes and game assets remain unchanged.
bool MarkPartsDirty(const Binding& binding) {
  Binding current{};
  std::uint32_t dirty = 0;
  if (!ReadBinding(current) || current != binding ||
      !GuardedRead(binding.alternate + 0x9c, dirty)) return false;
  if ((dirty & kPartsDirty) != 0) return true;
  dirty |= kPartsDirty;
  return GuardedWriteBytes(binding.alternate + 0x9c, &dirty, sizeof(dirty));
}

void __fastcall QueuePartsDetour(void* queue, std::uint32_t id, const void* records, int count) {
  ++g_visits;
  if (!g_enabled.load()) {
    g_original_queue(queue, id, records, count);
    return;
  }
  Binding binding{};
  if (!ReadBinding(binding) || id != binding.id ||
      reinterpret_cast<std::uintptr_t>(records) != binding.parts || count != kPartCount) {
    ++g_refused;
    g_original_queue(queue, id, records, count);
    return;
  }
  const unsigned mask = FilterMask();
  if (!mask) {
    Remember(binding, 0);
    g_original_queue(queue, id, records, count);
    return;
  }
  Parts original{}, scratch{}, verified{};
  Binding current{};
  if (!GuardedReadBytes(binding.parts, original.data(), original.size())) {
    ++g_refused;
    g_original_queue(queue, id, records, count);
    return;
  }
  scratch = original;
  std::uintptr_t slot_zero = 0;
  std::memcpy(&slot_zero, original.data(), sizeof(slot_zero));
  if ((mask & 1u) && binding.generated_face && slot_zero == binding.generated_face)
      scratch[9] &= 0xf0;
  for (unsigned slot = 1; slot <= 6u; ++slot)
      if (mask & (1u << slot)) scratch[slot * 16 + 9] &= 0xf0;
    // The live records must still be the ones just read and the mask must still
    // be wanted: a scene swap or an operator un-tick between the read and the
    // queue hands the native game its own unfiltered copy instead of a stale
    // filtered one.
    if (!ReadBinding(current) || current != binding ||
      !GuardedReadBytes(binding.parts, verified.data(), verified.size()) ||
        verified != original || FilterMask() != mask) {
    ++g_refused;
    g_original_queue(queue, id, records, count);
    return;
  }
  Remember(binding, mask);
  ++g_filtered;
  // Native 219230 copies all 13 records into the command buffer synchronously.
  g_original_queue(queue, id, scratch.data(), count);
}

void __fastcall ModelUpdateDetour(void* model, float delta) {
  if (g_enabled.load()) {
    AppliedParts applied{};
    {
      std::lock_guard<std::mutex> lock(g_applied_mutex);
      applied = g_applied;
    }
    const auto address = reinterpret_cast<std::uintptr_t>(model);
    const bool known = applied.valid && applied.binding.alternate == address;
    // Normal frames require no skeleton/parts scan or additional queue command.
    // Nonlocal models do not acquire XR state or copy the full mod config.
    if (known || IsLocalAlternate(address)) {
      const unsigned mask = FilterMask();
      if ((!known && mask) || (known && applied.mask != mask)) {
        Binding binding{};
        if (ReadBinding(binding) && binding.alternate == address) {
          if (MarkPartsDirty(binding)) ++g_refreshes;
          else ++g_refused;
        }
      }
    }
  }
  g_original_update(model, delta);
}
}  // namespace

// A queue command is already pending when the dirty bit is set, so an
// already-dirty binding counts as refreshed: the native updater will re-read the
// part records and the detour will then apply whatever mask is live at that
// moment, which is the config the caller just changed.
bool BodyVisibilityRequestRefresh() {
  if (!g_enabled.load()) return false;
  Binding binding{};
  if (!ReadBinding(binding)) return false;
  if (MarkPartsDirty(binding)) { ++g_refreshes; return true; }
  ++g_refused;
  return false;
}

bool BodyVisibilityHookInstall(bool enabled) {
  if (!enabled) return false;
  std::lock_guard<std::mutex> lock(HookInstallMutex());
  const auto base = ModuleBase(nullptr);
  if (!base || !BytesMatch(base + kQueueParts, kQueueBytes, sizeof(kQueueBytes)) ||
      !BytesMatch(base + kModelUpdate, kUpdateBytes, sizeof(kUpdateBytes))) return false;
  const auto initialized = MH_Initialize();
  if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) return false;
  if (!g_original_queue && MH_CreateHook(reinterpret_cast<void*>(base + kQueueParts),
      reinterpret_cast<void*>(QueuePartsDetour), reinterpret_cast<void**>(&g_original_queue)) != MH_OK) return false;
  if (!g_original_update && MH_CreateHook(reinterpret_cast<void*>(base + kModelUpdate),
      reinterpret_cast<void*>(ModelUpdateDetour), reinterpret_cast<void**>(&g_original_update)) != MH_OK) return false;
  const auto queue = MH_EnableHook(reinterpret_cast<void*>(base + kQueueParts));
  const auto update = MH_EnableHook(reinterpret_cast<void*>(base + kModelUpdate));
  const bool installed = (queue == MH_OK || queue == MH_ERROR_ENABLED) &&
      (update == MH_OK || update == MH_ERROR_ENABLED);
  if (!installed) {
    MH_DisableHook(reinterpret_cast<void*>(base + kQueueParts));
    MH_DisableHook(reinterpret_cast<void*>(base + kModelUpdate));
  }
  g_enabled.store(installed);
  return installed;
}

void BodyVisibilityHookUninstall() {
  g_enabled.store(false);
  std::lock_guard<std::mutex> lock(HookInstallMutex());
  AppliedParts applied{};
  {
    std::lock_guard<std::mutex> applied_lock(g_applied_mutex);
    applied = g_applied;
    g_applied = {};
  }
  if (applied.valid && applied.mask && !MarkPartsDirty(applied.binding)) ++g_refused;
  const auto base = ModuleBase(nullptr);
  if (g_original_update) MH_DisableHook(reinterpret_cast<void*>(base + kModelUpdate));
  if (g_original_queue) MH_DisableHook(reinterpret_cast<void*>(base + kQueueParts));
  // Keep both trampolines for callbacks already in flight.
}

BodyVisibilityStatus BodyVisibilityGetStatus() {
  return {g_enabled.load(), g_visits.load(), g_filtered.load(), g_refused.load(), g_refreshes.load()};
}
}  // namespace dsvr
