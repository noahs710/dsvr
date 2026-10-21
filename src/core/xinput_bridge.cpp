#include "xinput_bridge.h"

#include <windows.h>
#include <Xinput.h>
#include <MinHook.h>

#include <atomic>
#include <cstdint>
#include <cstring>

#include "../common/logging.h"
#include "../common/memory.h"
#include "input.h"
#include "pad_math.h"
#include "hook_install.h"
#include "lab_runner.h"

namespace dsvr {
namespace {

using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
std::atomic<XInputGetStateFn> g_original{nullptr};
void* g_target = nullptr;
std::atomic<unsigned long long> g_last_hash{0};
std::atomic<DWORD> g_packet{1};
std::atomic<bool> g_logged_first_override{false};

extern "C" std::uintptr_t DsvrIatCompareExchange(void* slot, std::uintptr_t expected,
                                                std::uintptr_t desired);
std::atomic<XInputGetStateFn> g_iat_original{nullptr};
std::atomic<bool> g_iat_enabled{false};
void* g_iat_slot = nullptr;
HMODULE g_iat_image = nullptr;
DWORD g_iat_restore_protection = 0;
bool g_iat_protection_pending = false;
DWORD WINAPI XInputIatDetour(DWORD, XINPUT_STATE*);

template <typename T>
bool ReadImage(std::uintptr_t base, std::size_t size, std::uint64_t offset, T& out) {
  return offset <= size && sizeof(T) <= size - offset &&
         GuardedRead(base + static_cast<std::uintptr_t>(offset), out);
}

bool ImageString(std::uintptr_t base, std::size_t size, std::uint64_t offset,
                 char (&out)[64]) {
  for (std::size_t i = 0; i < sizeof(out); ++i) {
    if (!ReadImage(base, size, offset + i, out[i])) return false;
    if (!out[i]) return true;
  }
  return false;
}

void* FindXInputIat(HMODULE image, HMODULE provider, void* target) {
  const auto base = reinterpret_cast<std::uintptr_t>(image);
  const std::size_t size = ModuleSize(base);
  IMAGE_DOS_HEADER dos{};
  IMAGE_NT_HEADERS64 nt{};
  if (!ReadImage(base, size, 0, dos) || dos.e_lfanew < 0 ||
      !ReadImage(base, size, dos.e_lfanew, nt) ||
      nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
      nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
      nt.OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_IMPORT)
    return nullptr;
  const auto dir = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (!dir.VirtualAddress || dir.VirtualAddress > size ||
      dir.Size > size - dir.VirtualAddress) return nullptr;
  void* found = nullptr;
  for (std::size_t off = 0; off + sizeof(IMAGE_IMPORT_DESCRIPTOR) <= dir.Size;
       off += sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
    IMAGE_IMPORT_DESCRIPTOR descriptor{};
    if (!ReadImage(base, size, std::uint64_t(dir.VirtualAddress) + off, descriptor))
      return nullptr;
    if (!descriptor.Name)
      return !descriptor.FirstThunk && !descriptor.OriginalFirstThunk ? found : nullptr;
    char dll[64]{};
    if (!ImageString(base, size, descriptor.Name, dll)) return nullptr;
    if (_stricmp(dll, "xinput1_3.dll") && _stricmp(dll, "xinput1_4.dll") &&
        _stricmp(dll, "xinput9_1_0.dll")) continue;
    if (GetModuleHandleA(dll) != provider) continue;
    if (!descriptor.OriginalFirstThunk || !descriptor.FirstThunk) return nullptr;
    bool terminated = false;
    for (std::uint64_t i = 0; i < 4096; ++i) {
      IMAGE_THUNK_DATA64 lookup{};
      const auto delta = i * sizeof(lookup);
      if (!ReadImage(base, size, std::uint64_t(descriptor.OriginalFirstThunk) + delta,
                     lookup)) return nullptr;
      if (!lookup.u1.AddressOfData) { terminated = true; break; }
      FARPROC imported = nullptr;
      if (IMAGE_SNAP_BY_ORDINAL64(lookup.u1.Ordinal)) {
        imported = GetProcAddress(provider, MAKEINTRESOURCEA(
            static_cast<WORD>(IMAGE_ORDINAL64(lookup.u1.Ordinal))));
      } else {
        char name[64]{};
        if (lookup.u1.AddressOfData > size ||
            !ImageString(base, size, lookup.u1.AddressOfData +
                offsetof(IMAGE_IMPORT_BY_NAME, Name), name)) return nullptr;
        if (std::strcmp(name, "XInputGetState") == 0)
          imported = GetProcAddress(provider, name);
      }
      if (reinterpret_cast<void*>(imported) != target) continue;
      std::uintptr_t current = 0;
      const auto offset = std::uint64_t(descriptor.FirstThunk) + delta;
      if (found || !ReadImage(base, size, offset, current) ||
          current != reinterpret_cast<std::uintptr_t>(target)) return nullptr;
      found = reinterpret_cast<void*>(base + static_cast<std::uintptr_t>(offset));
    }
    if (!terminated) return nullptr;
  }
  return nullptr;
}

bool PinBridgeLifetime(void* provider_export, void* detour) {
  HMODULE pinned = nullptr;
  const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN;
  return GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(provider_export), &pinned) &&
         GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(detour), &pinned);
}

bool ExchangeIat(void* expected, void* desired, bool& exchanged) {
  exchanged = false;
  MEMORY_BASIC_INFORMATION info{};
  const auto address = reinterpret_cast<std::uintptr_t>(g_iat_slot);
  // The pinned x64 image uses four-mod-eight thunks. Use native LOCK CMPXCHG,
  // not an unaligned std::atomic or an undocumented Win32 interlocked cast.
  if (!g_iat_slot || (address & 63) > 64 - sizeof(void*) ||
      !VirtualQuery(g_iat_slot, &info, sizeof(info)) ||
      info.AllocationBase != g_iat_image || info.State != MEM_COMMIT ||
      (info.Protect & (PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE)) ||
      info.Protect == PAGE_NOACCESS ||
      address + sizeof(void*) > reinterpret_cast<std::uintptr_t>(info.BaseAddress) +
                                  info.RegionSize) return false;
  DWORD old = 0;
  const DWORD writable = (info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
      PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) ?
      PAGE_EXECUTE_READWRITE : PAGE_READWRITE;
  if (!VirtualProtect(g_iat_slot, sizeof(void*), writable, &old)) return false;
  const DWORD restore = g_iat_protection_pending ? g_iat_restore_protection : old;
  g_iat_restore_protection = restore;
  exchanged = DsvrIatCompareExchange(g_iat_slot,
      reinterpret_cast<std::uintptr_t>(expected),
      reinterpret_cast<std::uintptr_t>(desired)) ==
      reinterpret_cast<std::uintptr_t>(expected);
  DWORD ignored = 0;
  g_iat_protection_pending =
      !VirtualProtect(g_iat_slot, sizeof(void*), restore, &ignored);
  if (g_iat_protection_pending) DSVR_ERROR("XInput: IAT page protection restore failed");
  return !g_iat_protection_pending;
}

void RemoveXInputIat() {
  g_iat_enabled.store(false, std::memory_order_release);
  if (!g_iat_slot) return;
  bool exchanged = false;
  if (!ExchangeIat(reinterpret_cast<void*>(&XInputIatDetour),
      reinterpret_cast<void*>(g_iat_original.load()), exchanged)) {
    DSVR_ERROR("XInput: IAT cleanup incomplete; disabled, retaining restore state");
    return;
  }
  if (!exchanged) DSVR_WARN("XInput: IAT owner changed; leaving foreign entry untouched");
  g_iat_slot = nullptr;
  g_iat_image = nullptr;
}

bool InstallXInputIat(HMODULE provider, void* target) {
  HMODULE image = GetModuleHandleW(nullptr);
  void* slot = FindXInputIat(image, provider, target);
  if (!slot || (reinterpret_cast<std::uintptr_t>(slot) & 63) > 64 - sizeof(void*)) {
    DSVR_ERROR("XInput: no unique owned game XInputGetState import; IAT fallback refused");
    return false;
  }
  const auto original = reinterpret_cast<XInputGetStateFn>(target);
  const auto retained = g_iat_original.load(std::memory_order_acquire);
  if (retained && retained != original) return false;
  if (!PinBridgeLifetime(target, reinterpret_cast<void*>(&XInputIatDetour))) return false;
  g_iat_original.store(original, std::memory_order_release);
  g_iat_image = image;
  g_iat_slot = slot;
  bool exchanged = false;
  if (!ExchangeIat(target, reinterpret_cast<void*>(&XInputIatDetour), exchanged) || !exchanged) {
    RemoveXInputIat();
    return false;
  }
  g_iat_enabled.store(true, std::memory_order_release);
  DSVR_LOG("XInput: game-local IAT fallback ready (no trampoline allocation)");
  return true;
}

std::atomic<bool> g_inline_enabled{false};
std::atomic<bool> g_logged_first_iat_poll{false};

bool EnableInlineBridge() {
  const auto status = MH_EnableHook(g_target);
  if (status != MH_OK && status != MH_ERROR_ENABLED) {
    DSVR_ERROR("XInput: owned hook activation failed (%s)", MH_StatusToString(status));
    return false;
  }
  g_inline_enabled.store(true, std::memory_order_release);
  DSVR_LOG("XInput: owned inline bridge enabled");
  return true;
}

VirtualPadState FromXInput(const XINPUT_GAMEPAD& in) {
  VirtualPadState out;
  out.buttons = in.wButtons;
  out.left_trigger = in.bLeftTrigger;
  out.right_trigger = in.bRightTrigger;
  out.thumb_lx = in.sThumbLX;
  out.thumb_ly = in.sThumbLY;
  out.thumb_rx = in.sThumbRX;
  out.thumb_ry = in.sThumbRY;
  return out;
}

void ToXInput(const VirtualPadState& in, XINPUT_GAMEPAD& out) {
  out.wButtons = in.buttons;
  out.bLeftTrigger = in.left_trigger;
  out.bRightTrigger = in.right_trigger;
  out.sThumbLX = in.thumb_lx;
  out.sThumbLY = in.thumb_ly;
  out.sThumbRX = in.thumb_rx;
  out.sThumbRY = in.thumb_ry;
}

unsigned long long HashPad(const XINPUT_GAMEPAD& pad) {
  const auto* bytes = reinterpret_cast<const unsigned char*>(&pad);
  unsigned long long hash = 1469598103934665603ull;
  for (std::size_t i = 0; i < sizeof(pad); ++i) {
    hash ^= bytes[i];
    hash *= 1099511628211ull;
  }
  return hash;
}

DWORD MergeXInputState(DWORD user_index, XINPUT_STATE* state, DWORD result) {
  if (user_index != 0) return result;
  VirtualPadState motion{};
  const bool have_motion = InputLatestPad(motion);
  const VirtualPadState physical = result == ERROR_SUCCESS ?
      FromXInput(state->Gamepad) : VirtualPadState{};
  VirtualPadState merged{};
  const bool lab_owned = LabPollPad(physical, motion, merged);
  if (!lab_owned && !have_motion) return result;
  if (!lab_owned && !g_logged_first_override.exchange(true)) {
    DSVR_LOG("XInput: first Quest pad state fed to the game (bridge live; "
             "this line proves the game polls XInputGetState)");
  }
  if (result != ERROR_SUCCESS) std::memset(state, 0, sizeof(*state));
  if (!lab_owned) merged = OverlayMotionPad(physical, motion);
  ToXInput(merged, state->Gamepad);
  const unsigned long long hash = HashPad(state->Gamepad);
  unsigned long long previous = g_last_hash.load(std::memory_order_relaxed);
  if (previous != hash && g_last_hash.compare_exchange_strong(
                              previous, hash, std::memory_order_relaxed)) {
    g_packet.fetch_add(1, std::memory_order_relaxed);
  }
  state->dwPacketNumber = g_packet.load(std::memory_order_relaxed);
  return ERROR_SUCCESS;
}

DWORD WINAPI XInputGetStateDetour(DWORD user_index, XINPUT_STATE* state) {
  if (!state) return ERROR_BAD_ARGUMENTS;
  const auto original = g_original.load(std::memory_order_acquire);
  if (!original) return ERROR_DEVICE_NOT_CONNECTED;
  const DWORD result = original(user_index, state);
  if (!g_inline_enabled.load(std::memory_order_acquire)) return result;
  return MergeXInputState(user_index, state, result);
}

DWORD WINAPI XInputIatDetour(DWORD user_index, XINPUT_STATE* state) {
  if (!state) return ERROR_BAD_ARGUMENTS;
  const auto original = g_iat_original.load(std::memory_order_acquire);
  if (!original) return ERROR_DEVICE_NOT_CONNECTED;
  const DWORD result = original(user_index, state);
  if (!g_iat_enabled.load(std::memory_order_acquire)) return result;
  if (user_index == 0 && !g_logged_first_iat_poll.exchange(true))
    DSVR_LOG("XInput: first game poll through IAT fallback (user=0, native_result=%lu; XR state separate)", result);
  return MergeXInputState(user_index, state, result);
}

}  // namespace

bool XInputBridgeInstall(bool enabled) {
  std::lock_guard<std::mutex> install_lock(HookInstallMutex());
  if (!enabled) {
    DSVR_LOG("XInput: OpenXR bridge disabled with xr_input_enabled=false");
    return false;
  }
  if (g_iat_slot) return g_iat_enabled.load(std::memory_order_acquire);
  if (g_original.load(std::memory_order_acquire))
    return g_inline_enabled.load(std::memory_order_acquire) || EnableInlineBridge();
  HMODULE module = nullptr;
  const wchar_t* candidates[] = {L"xinput1_3.dll", L"xinput1_4.dll", L"xinput9_1_0.dll"};
  const wchar_t* module_name = nullptr;
  for (const wchar_t* name : candidates) {
    module = GetModuleHandleW(name);
    if (!module) module = LoadLibraryW(name);
    if (module) { module_name = name; break; }
  }
  if (!module) {
    DSVR_ERROR("XInput: no XInput DLL loadable; Quest gamepad bridge unavailable");
    return false;
  }
  g_target = reinterpret_cast<void*>(GetProcAddress(module, "XInputGetState"));
  if (!g_target) {
    DSVR_ERROR("XInput: XInputGetState export missing in %ls; bridge unavailable", module_name);
    return false;
  }
  DSVR_LOG("XInput: hooking XInputGetState in %ls", module_name);
  const MH_STATUS init = MH_Initialize();
  if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
    DSVR_ERROR("XInput: MinHook initialize failed (%s)", MH_StatusToString(init));
    g_target = nullptr;
    return false;
  }
  MH_STATUS create = MH_ERROR_NOT_INITIALIZED;
  XInputGetStateFn original = nullptr;
  for (int attempt = 0; attempt < 3; ++attempt) {
    if (attempt) Sleep(5);
    original = nullptr;
    create = MH_CreateHook(g_target, reinterpret_cast<void*>(&XInputGetStateDetour),
                           reinterpret_cast<void**>(&original));
    if (create == MH_OK) break;
    DSVR_WARN("XInput: create attempt %d failed (%s)", attempt + 1, MH_StatusToString(create));
    if (create != MH_ERROR_MEMORY_ALLOC) break;
  }
  if (create != MH_OK) {
    void* target = g_target;
    g_target = nullptr;
    DSVR_WARN("XInput: XInputGetState hook creation failed (%s)", MH_StatusToString(create));
    return create == MH_ERROR_MEMORY_ALLOC && InstallXInputIat(module, target);
  }
  if (!PinBridgeLifetime(g_target, reinterpret_cast<void*>(&XInputGetStateDetour))) {
    MH_RemoveHook(g_target);  // Ours, and not published/enabled yet.
    g_target = nullptr;
    return false;
  }
  g_original.store(original, std::memory_order_release);
  return EnableInlineBridge();
}

void XInputBridgeUninstall() {
  std::lock_guard<std::mutex> install_lock(HookInstallMutex());
  RemoveXInputIat();
  g_inline_enabled.store(false, std::memory_order_release);
  if (!g_original.load(std::memory_order_acquire) || !g_target) return;
  const auto status = MH_DisableHook(g_target);
  if (status != MH_OK && status != MH_ERROR_DISABLED)
    DSVR_WARN("XInput: hook disable failed (%s); retained callback is pass-through",
              MH_StatusToString(status));
  // A late/in-flight callback can still call its trampoline after disabling.
  // Keep our one owned hook and pinned modules alive; re-enable it on reinstall.
}

}  // namespace dsvr
