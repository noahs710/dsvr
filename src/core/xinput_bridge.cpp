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
#include "iat_patch.h"
#include "lab_runner.h"

namespace dsvr {
namespace {

using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
using XInputGetCapabilitiesFn = DWORD(WINAPI*)(DWORD, DWORD, XINPUT_CAPABILITIES*);
using XInputSetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);
// Untyped like the IAT slots: both routes of one export share one detour body
// (MergeRoute), which names the function type at the call site.
std::atomic<void*> g_original{nullptr};
std::atomic<void*> g_caps_original{nullptr};
std::atomic<void*> g_setstate_original{nullptr};
void* g_target = nullptr;
void* g_caps_target = nullptr;
void* g_setstate_target = nullptr;
std::atomic<unsigned long long> g_last_hash{0};
std::atomic<DWORD> g_packet{1};
std::atomic<bool> g_logged_first_override{false};
std::atomic<bool> g_logged_first_caps{false};
std::atomic<bool> g_logged_first_rumble{false};
// Read-only poll watch: what keyboard/API state the game reads while the
// front-end is up. Never modifies results; uninstall removes the patches.
using GetAsyncKeyStateFn = SHORT(WINAPI*)(int);
using GetKeyboardStateFn = BOOL(WINAPI*)(PBYTE);
using DirectInput8CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, void*);
iat::IatPatch g_askeys_iat;
iat::IatPatch g_kbstate_iat;
iat::IatPatch g_dicreate_iat;
std::atomic<unsigned long long> g_askeys_calls{0};
std::atomic<unsigned long long> g_askeys_down{0};
std::atomic<unsigned long long> g_askeys_last_summary{0};
std::atomic<unsigned long long> g_kbstate_calls{0};
std::atomic<unsigned long long> g_kbstate_down{0};
std::atomic<unsigned long long> g_kbstate_last_summary{0};
std::atomic<int> g_first_key_down{-1};

void MaybePollSummary(const char* what, unsigned long long calls,
                      unsigned long long down,
                      std::atomic<unsigned long long>& last) {
  const auto now = GetTickCount64();
  auto prev = last.load(std::memory_order_relaxed);
  if ((!prev || now < prev || now - prev >= 60000) &&
      last.compare_exchange_strong(prev, now, std::memory_order_relaxed))
    DSVR_LOG("Input: %s polls=%llu down=%llu (60s window)", what, calls, down);
}

extern "C" std::uintptr_t DsvrIatCompareExchange(void* slot, std::uintptr_t expected,
                                                 std::uintptr_t desired);
// One game-local IAT patch per export. The pinned game enumerates through
// XInputGetState. Capabilities is best-effort for other callers; a missing
// capabilities import does not mean the state bridge or enumeration failed.
struct IatPatch {
  std::atomic<void*> original{nullptr};
  std::atomic<bool> enabled{false};
  void* slot = nullptr;
  HMODULE image = nullptr;
  DWORD restore_protection = 0;
  bool protection_pending = false;
};
IatPatch g_state_iat;
IatPatch g_caps_iat;
IatPatch g_setstate_iat;
DWORD WINAPI XInputIatDetour(DWORD, XINPUT_STATE*);
DWORD WINAPI XInputCapsIatDetour(DWORD, DWORD, XINPUT_CAPABILITIES*);
DWORD WINAPI XInputSetStateDetour(DWORD, XINPUT_VIBRATION*);
DWORD WINAPI XInputSetStateIatDetour(DWORD, XINPUT_VIBRATION*);

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

bool ExchangeIat(IatPatch& patch, void* expected, void* desired, bool& exchanged) {
  exchanged = false;
  MEMORY_BASIC_INFORMATION info{};
  const auto address = reinterpret_cast<std::uintptr_t>(patch.slot);
  // The pinned x64 image uses four-mod-eight thunks. Use native LOCK CMPXCHG,
  // not an unaligned std::atomic or an undocumented Win32 interlocked cast.
  if (!patch.slot || (address & 63) > 64 - sizeof(void*) ||
      !VirtualQuery(patch.slot, &info, sizeof(info)) ||
      info.AllocationBase != patch.image || info.State != MEM_COMMIT ||
      (info.Protect & (PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE)) ||
      info.Protect == PAGE_NOACCESS ||
      address + sizeof(void*) > reinterpret_cast<std::uintptr_t>(info.BaseAddress) +
                                  info.RegionSize) return false;
  DWORD old = 0;
  const DWORD writable = (info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
      PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) ?
      PAGE_EXECUTE_READWRITE : PAGE_READWRITE;
  if (!VirtualProtect(patch.slot, sizeof(void*), writable, &old)) return false;
  const DWORD restore = patch.protection_pending ? patch.restore_protection : old;
  patch.restore_protection = restore;
  exchanged = DsvrIatCompareExchange(patch.slot,
      reinterpret_cast<std::uintptr_t>(expected),
      reinterpret_cast<std::uintptr_t>(desired)) ==
      reinterpret_cast<std::uintptr_t>(expected);
  DWORD ignored = 0;
  patch.protection_pending =
      !VirtualProtect(patch.slot, sizeof(void*), restore, &ignored);
  if (patch.protection_pending) DSVR_ERROR("XInput: IAT page protection restore failed");
  return !patch.protection_pending;
}

void RemoveXInputIat(IatPatch& patch, void* detour) {
  patch.enabled.store(false, std::memory_order_release);
  if (!patch.slot) return;
  bool exchanged = false;
  if (!ExchangeIat(patch, detour, patch.original.load(), exchanged)) {
    DSVR_ERROR("XInput: IAT cleanup incomplete; disabled, retaining restore state");
    return;
  }
  if (!exchanged) DSVR_WARN("XInput: IAT owner changed; leaving foreign entry untouched");
  patch.slot = nullptr;
  patch.image = nullptr;
}

bool InstallXInputIat(HMODULE provider, void* target, IatPatch& patch,
                      void* detour, const char* what) {
  HMODULE image = GetModuleHandleW(nullptr);
  void* slot = FindXInputIat(image, provider, target);
  if (!slot || (reinterpret_cast<std::uintptr_t>(slot) & 63) > 64 - sizeof(void*)) {
    DSVR_ERROR("XInput: no unique owned game %s import; IAT fallback refused", what);
    return false;
  }
  const auto retained = patch.original.load(std::memory_order_acquire);
  if (retained && retained != target) return false;
  if (!PinBridgeLifetime(target, detour)) return false;
  patch.original.store(target, std::memory_order_release);
  patch.image = image;
  patch.slot = slot;
  bool exchanged = false;
  if (!ExchangeIat(patch, target, detour, exchanged) || !exchanged) {
    RemoveXInputIat(patch, detour);
    return false;
  }
  patch.enabled.store(true, std::memory_order_release);
  DSVR_LOG("XInput: game-local IAT fallback ready for %s (no trampoline allocation)", what);
  return true;
}

std::atomic<bool> g_inline_enabled{false};
std::atomic<bool> g_caps_inline_enabled{false};
std::atomic<bool> g_setstate_inline_enabled{false};
std::atomic<bool> g_logged_first_iat_poll{false};

bool EnableInlineBridge(void* target, std::atomic<bool>& flag, const char* what) {
  const auto status = MH_EnableHook(target);
  if (status != MH_OK && status != MH_ERROR_ENABLED) {
    DSVR_ERROR("XInput: owned hook activation failed for %s (%s)", what,
               MH_StatusToString(status));
    return false;
  }
  flag.store(true, std::memory_order_release);
  DSVR_LOG("XInput: owned inline bridge enabled for %s", what);
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
  // The front-end may poll any user slot for its menus, so the same virtual
  // pad answers on users 0-3 (a Quest-only player has no physical pad on any
  // slot). User 4+ stays native passthrough.
  if (user_index > 3) return result;
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
  static std::atomic<ULONGLONG> last_route_log{0};
  const auto now = GetTickCount64();
  auto last = last_route_log.load(std::memory_order_relaxed);
  if ((!last || now < last || now - last >= 1000) &&
      last_route_log.compare_exchange_strong(last, now, std::memory_order_relaxed)) {
    DWORD foreground_pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foreground_pid);
    DSVR_LOG("XInputRoute: user=%lu native_result=%lu motion=%d lab=%d foreground=%d motion_buttons=0x%04x merged_buttons=0x%04x left=(%d,%d)",
             user_index, result, have_motion, lab_owned, foreground_pid == GetCurrentProcessId(),
             static_cast<unsigned>(motion.buttons), static_cast<unsigned>(merged.buttons),
             static_cast<int>(merged.thumb_lx), static_cast<int>(merged.thumb_ly));
  }
  const unsigned long long hash = HashPad(state->Gamepad);
  unsigned long long previous = g_last_hash.load(std::memory_order_relaxed);
  if (previous != hash && g_last_hash.compare_exchange_strong(
                              previous, hash, std::memory_order_relaxed)) {
    g_packet.fetch_add(1, std::memory_order_relaxed);
  }
  state->dwPacketNumber = g_packet.load(std::memory_order_relaxed);
  return ERROR_SUCCESS;
}

// One body for both routes of an export: call the saved original, then merge
// only while this route is enabled. The inline and IAT detours differ only in
// which slot holds the original and which flag gates the merge, so the pair
// cannot drift apart when the merge contract changes.
// The pointer argument is checked and forwarded last; the leading args (user
// index, and for capabilities the flags) pass through untouched. Fn is named
// explicitly because the slot stores an untyped pointer for both routes.
template <typename Fn, typename Merge, typename Pointer, typename... Lead>
DWORD MergeRoute(std::atomic<void*>& original_slot, const std::atomic<bool>& enabled,
                 Merge merge, Pointer* argument, Lead... lead) {
  if (!argument) return ERROR_BAD_ARGUMENTS;
  const auto original = reinterpret_cast<Fn>(original_slot.load(std::memory_order_acquire));
  if (!original) return ERROR_DEVICE_NOT_CONNECTED;
  const DWORD result = original(lead..., argument);
  if (!enabled.load(std::memory_order_acquire)) return result;
  return merge(lead..., argument, result);
}

DWORD WINAPI XInputGetStateDetour(DWORD user_index, XINPUT_STATE* state) {
  return MergeRoute<XInputGetStateFn>(g_original, g_inline_enabled,
                                      MergeXInputState, state, user_index);
}

DWORD WINAPI XInputIatDetour(DWORD user_index, XINPUT_STATE* state) {
  return MergeRoute<XInputGetStateFn>(
      g_state_iat.original, g_state_iat.enabled,
      [](DWORD user, XINPUT_STATE* s, DWORD result) {
        if (user == 0 && !g_logged_first_iat_poll.exchange(true))
          DSVR_LOG("XInput: first game poll through IAT fallback (user=0, native_result=%lu; XR state separate)", result);
        return MergeXInputState(user, s, result);
      },
      state, user_index);
}

// A Quest-only player has no physical rumble motors, so the native SetState
// answers ERROR_DEVICE_NOT_CONNECTED. Some front-ends treat a failed rumble
// as a missing pad and keep ignoring the state feed, so accept rumble for the
// virtual pads (users 0-3) without touching the disconnected driver. User 4+
// falls through to the saved original.
DWORD AcceptVirtualRumble(DWORD user_index, XINPUT_VIBRATION* vibration) {
  if (!vibration) return ERROR_BAD_ARGUMENTS;
  if (user_index > 3) return ERROR_DEVICE_NOT_CONNECTED;
  if (!g_logged_first_rumble.exchange(true))
    DSVR_LOG("XInput: game sent rumble to the virtual pad (accepted)");
  return ERROR_SUCCESS;
}

DWORD WINAPI XInputSetStateDetour(DWORD user_index, XINPUT_VIBRATION* vibration) {
  if (user_index <= 3 || !vibration) return AcceptVirtualRumble(user_index, vibration);
  return MergeRoute<XInputSetStateFn>(
      g_setstate_original, g_setstate_inline_enabled,
      [](DWORD, XINPUT_VIBRATION*, DWORD result) { return result; },
      vibration, user_index);
}

DWORD WINAPI XInputSetStateIatDetour(DWORD user_index, XINPUT_VIBRATION* vibration) {
  if (user_index <= 3 || !vibration) return AcceptVirtualRumble(user_index, vibration);
  return MergeRoute<XInputSetStateFn>(
      g_setstate_iat.original, g_setstate_iat.enabled,
      [](DWORD, XINPUT_VIBRATION*, DWORD result) { return result; },
      vibration, user_index);
}

// A Quest-only player has no physical XInput device, so the native call answers
// ERROR_DEVICE_NOT_CONNECTED and the game's front-end never treats users 0-3
// as pads at all: menus stay unreachable even while the state bridge is live.
// Report the standard virtual gamepad that the state detour actually feeds.
DWORD FillVirtualCapabilities(XINPUT_CAPABILITIES* caps) {
  std::memset(caps, 0, sizeof(*caps));
  caps->Type = XINPUT_DEVTYPE_GAMEPAD;
  caps->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
  caps->Flags = 0;
  caps->Gamepad.wButtons = static_cast<WORD>(
      XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN |
      XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT |
      XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_BACK |
      XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_RIGHT_THUMB |
      XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER |
      XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_Y);
  return ERROR_SUCCESS;
}

DWORD MergeXInputCapabilities(DWORD user_index, XINPUT_CAPABILITIES* caps,
                              DWORD result, const char* path) {
  if (user_index != 0) return result;
  if (result == ERROR_SUCCESS) return result;  // a physical pad's own caps win
  if (!g_logged_first_caps.exchange(true))
    DSVR_LOG("XInput: no physical pad on %s; reporting the virtual Quest gamepad "
             "(this is the gate the game's front-end checks)", path);
  return FillVirtualCapabilities(caps);
}

DWORD WINAPI XInputGetCapabilitiesDetour(DWORD user_index, DWORD flags,
                                         XINPUT_CAPABILITIES* caps) {
  return MergeRoute<XInputGetCapabilitiesFn>(
      g_caps_original, g_caps_inline_enabled,
      [](DWORD user, DWORD, XINPUT_CAPABILITIES* c, DWORD result) {
        return MergeXInputCapabilities(user, c, result, "inline hook");
      },
      caps, user_index, flags);
}

// The pinned game does not import XInputGetCapabilities, so neither hook
// route can reach it; it resolves the export dynamically instead and the
// unpatched resolution reports no connected pad while state already reports
// our virtual gamepad. Reroute that dynamic resolution through the game's own
// GetProcAddress import so capability queries agree with state.
using GetProcAddressFn = FARPROC(WINAPI*)(HMODULE, LPCSTR);
iat::IatPatch g_procaddress_iat;
std::atomic<bool> g_logged_caps_reroute{false};
bool ModuleIsXInput(HMODULE mod) {
  if (!mod) return false;
  const wchar_t* names[] = {L"xinput1_3.dll", L"xinput1_4.dll",
                            L"xinput9_1_0.dll"};
  for (const wchar_t* name : names) {
    HMODULE known = GetModuleHandleW(name);
    if (known && known == mod) return true;
  }
  return false;
}
DWORD WINAPI XInputVirtualCapsDetour(DWORD user_index, DWORD,
                                     XINPUT_CAPABILITIES* caps) {
  if (!caps) return ERROR_BAD_ARGUMENTS;
  return MergeXInputCapabilities(user_index, caps, ERROR_DEVICE_NOT_CONNECTED,
                                 "procaddress route");
}
void LogInterestingResolution(LPCSTR name) {
  static const char* kWatched[] = {
      "XInputGetCapabilities", "XInputGetState", "GetActiveWindow",
      "GetFocus", "IsWindowVisible", "GetForegroundWindow", "SetCapture",
      "GetCapture", "IsIconic", "GetWindow", "GetWindowTextW",
      "DirectInput8Create", "GetKeyboardState", "GetKeyState",
      "SetFocus", "SetActiveWindow", "ShowWindow"};
  static bool logged[sizeof(kWatched) / sizeof(kWatched[0])] = {};
  for (std::size_t i = 0; i < sizeof(kWatched) / sizeof(kWatched[0]); ++i) {
    if (!logged[i] && std::strcmp(name, kWatched[i]) == 0) {
      logged[i] = true;
      DSVR_LOG("XInput: game resolved %s dynamically", kWatched[i]);
      return;
    }
  }
}
FARPROC WINAPI XInputGetProcAddressDetour(HMODULE mod, LPCSTR name) {
  const auto original = reinterpret_cast<GetProcAddressFn>(
      g_procaddress_iat.original);
  FARPROC resolved = original ? original(mod, name) : nullptr;
  if (resolved && name &&
      (reinterpret_cast<std::uintptr_t>(name) >> 16) != 0) {
    LogInterestingResolution(name);
  }
  if (resolved && name &&
      (reinterpret_cast<std::uintptr_t>(name) >> 16) != 0 &&
      std::strcmp(name, "XInputGetCapabilities") == 0 &&
      ModuleIsXInput(mod)) {
    if (!g_logged_caps_reroute.exchange(true, std::memory_order_acq_rel))
      DSVR_LOG("XInput: rerouting dynamic XInputGetCapabilities resolution to the virtual gamepad");
    return reinterpret_cast<FARPROC>(&XInputVirtualCapsDetour);
  }
  return resolved;
}
DWORD WINAPI XInputCapsIatDetour(DWORD user_index, DWORD flags,
                                 XINPUT_CAPABILITIES* caps) {
  return MergeRoute<XInputGetCapabilitiesFn>(
      g_caps_iat.original, g_caps_iat.enabled,
      [](DWORD user, DWORD, XINPUT_CAPABILITIES* c, DWORD result) {
        return MergeXInputCapabilities(user, c, result, "IAT fallback");
      },
      caps, user_index, flags);
}

// Intercept one xinput export: an owned MinHook trampoline when trampoline
// allocation succeeds, otherwise a game-local IAT patch. Per-export state, so a
// failure on one export never tears down the other.
template <typename Fn>
bool BridgeExport(HMODULE module, const char* name, void*& target,
                  std::atomic<void*>& original_out, void* detour,
                  void* iat_detour, std::atomic<bool>& inline_flag, IatPatch& iat) {
  if (iat.slot) return iat.enabled.load(std::memory_order_acquire);
  if (original_out.load(std::memory_order_acquire))
    return inline_flag.load(std::memory_order_acquire) ||
           EnableInlineBridge(target, inline_flag, name);
  target = reinterpret_cast<void*>(GetProcAddress(module, name));
  if (!target) {
    DSVR_ERROR("XInput: %s export missing; bridge incomplete", name);
    return false;
  }
  MH_STATUS create = MH_ERROR_NOT_INITIALIZED;
  Fn original = nullptr;
  for (int attempt = 0; attempt < 3; ++attempt) {
    if (attempt) Sleep(5);
    original = nullptr;
    create = MH_CreateHook(target, detour, reinterpret_cast<void**>(&original));
    if (create == MH_OK) break;
    DSVR_WARN("XInput: %s create attempt %d failed (%s)", name, attempt + 1,
              MH_StatusToString(create));
    if (create != MH_ERROR_MEMORY_ALLOC) break;
  }
  if (create != MH_OK) {
    DSVR_WARN("XInput: %s hook creation failed (%s)", name, MH_StatusToString(create));
    // The inline detour reads the MinHook trampoline, which never exists once
    // creation failed; the IAT detour reads the slot's own saved original.
    return create == MH_ERROR_MEMORY_ALLOC &&
           InstallXInputIat(module, target, iat, iat_detour, name);
  }
  if (!PinBridgeLifetime(target, detour)) {
    MH_RemoveHook(target);  // Ours, and not published/enabled yet.
    target = nullptr;
    return false;
  }
  original_out.store(reinterpret_cast<void*>(original), std::memory_order_release);
  return EnableInlineBridge(target, inline_flag, name);
}

}  // namespace

bool NoteFirstKeyDown(int vkey) {
  int expected = -1;
  return vkey >= 0 && vkey < 256 &&
         g_first_key_down.compare_exchange_strong(expected, vkey,
                                                 std::memory_order_acq_rel);
}

SHORT WINAPI LogGetAsyncKeyState(int vkey) {
  const auto orig = reinterpret_cast<GetAsyncKeyStateFn>(
      g_askeys_iat.original);
  const SHORT result = orig ? orig(vkey) : 0;
  const unsigned long long n =
      g_askeys_calls.fetch_add(1, std::memory_order_relaxed) + 1;
  if (n == 1)
    DSVR_LOG("Input: game polls GetAsyncKeyState (global keyboard path live)");
  if (result & 0x8000) {
    g_askeys_down.fetch_add(1, std::memory_order_relaxed);
    if (NoteFirstKeyDown(vkey))
      DSVR_LOG("Input: game saw async key down (vkey=0x%02x)", vkey & 0xff);
  }
  MaybePollSummary("GetAsyncKeyState",
                   g_askeys_calls.load(std::memory_order_relaxed),
                   g_askeys_down.load(std::memory_order_relaxed),
                   g_askeys_last_summary);
  return result;
}

BOOL WINAPI LogGetKeyboardState(PBYTE keys) {
  const auto orig = reinterpret_cast<GetKeyboardStateFn>(
      g_kbstate_iat.original);
  const BOOL result = orig ? orig(keys) : FALSE;
  const unsigned long long n =
      g_kbstate_calls.fetch_add(1, std::memory_order_relaxed) + 1;
  if (n == 1)
    DSVR_LOG("Input: game polls GetKeyboardState (thread-queue keyboard path live)");
  if (result && keys) {
    for (int i = 0; i < 256; ++i) {
      if (keys[i] & 0x80) {
        g_kbstate_down.fetch_add(1, std::memory_order_relaxed);
        if (NoteFirstKeyDown(i))
          DSVR_LOG("Input: game saw thread-queue key down (vkey=0x%02x)", i);
        break;
      }
    }
  }
  MaybePollSummary("GetKeyboardState",
                   g_kbstate_calls.load(std::memory_order_relaxed),
                   g_kbstate_down.load(std::memory_order_relaxed),
                   g_kbstate_last_summary);
  return result;
}

HRESULT WINAPI LogDirectInput8Create(HINSTANCE instance, DWORD version,
                                     REFIID riid, LPVOID* out,
                                     void* outer) {
  const auto orig = reinterpret_cast<DirectInput8CreateFn>(
      g_dicreate_iat.original);
  const HRESULT result = orig ? orig(instance, version, riid, out, outer)
                              : E_FAIL;
  DSVR_LOG("Input: game called DirectInput8Create (version=0x%08lx result=0x%08lx)",
           version, static_cast<unsigned long>(result));
  return result;
}

bool XInputBridgeInstall(bool enabled) {
  std::lock_guard<std::mutex> install_lock(HookInstallMutex());
  if (!enabled) {
    DSVR_LOG("XInput: OpenXR bridge disabled with xr_input_enabled=false");
    return false;
  }
  // Each export restores its own route. State may use an IAT patch while
  // capabilities owns an inline trampoline (or the reverse); one retained
  // trampoline must not make us try enabling a hook we never created.
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
  DSVR_LOG("XInput: hooking XInputGetState + XInputGetCapabilities in %ls", module_name);
  const MH_STATUS init = MH_Initialize();
  if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
    DSVR_ERROR("XInput: MinHook initialize failed (%s)", MH_StatusToString(init));
    g_target = nullptr;
    g_caps_target = nullptr;
    return false;
  }
  const bool state_ok = BridgeExport<XInputGetStateFn>(
      module, "XInputGetState", g_target, g_original,
      reinterpret_cast<void*>(&XInputGetStateDetour),
      reinterpret_cast<void*>(&XInputIatDetour), g_inline_enabled, g_state_iat);
  if (!state_ok) {
    // State is the load-bearing hook. Reporting a connected pad through
    // capabilities while the state feed is dead leaves the front-end waiting on
    // a controller that can never answer, which is worse than no pad at all
    // (the game then falls back to keyboard/mouse for menus).
    DSVR_WARN("XInput: state bridge failed; capabilities hook skipped");
    return false;
  }
  if (!BridgeExport<XInputGetCapabilitiesFn>(
          module, "XInputGetCapabilities", g_caps_target, g_caps_original,
          reinterpret_cast<void*>(&XInputGetCapabilitiesDetour),
          reinterpret_cast<void*>(&XInputCapsIatDetour), g_caps_inline_enabled,
          g_caps_iat))
    DSVR_WARN("XInput: capabilities route unavailable; the verified state route remains live");
  if (!BridgeExport<XInputSetStateFn>(
          module, "XInputSetState", g_setstate_target, g_setstate_original,
          reinterpret_cast<void*>(&XInputSetStateDetour),
          reinterpret_cast<void*>(&XInputSetStateIatDetour), g_setstate_inline_enabled,
          g_setstate_iat))
    DSVR_WARN("XInput: rumble route unavailable; the verified state route remains live");
  if (!iat::InstallIat(GetModuleHandleA("kernel32.dll"), "kernel32.dll",
                       "GetProcAddress", g_procaddress_iat,
                       reinterpret_cast<void*>(&XInputGetProcAddressDetour),
                       "XInputProcAddress"))
    DSVR_WARN("XInput: GetProcAddress route unavailable; dynamic capability queries stay unpatched");
  // Read-only poll watch: which keyboard/API state the front-end reads.
  // Best-effort and independent of the state bridge; a missing import only
  // loses the corresponding observation, never input.
  if (!iat::InstallIat(GetModuleHandleA("user32.dll"), "user32.dll",
                       "GetAsyncKeyState", g_askeys_iat,
                       reinterpret_cast<void*>(&LogGetAsyncKeyState),
                       "KeyPollWatch"))
    DSVR_WARN("Input: GetAsyncKeyState watch unavailable; async-key polling stays unobserved");
  if (!iat::InstallIat(GetModuleHandleA("user32.dll"), "user32.dll",
                       "GetKeyboardState", g_kbstate_iat,
                       reinterpret_cast<void*>(&LogGetKeyboardState),
                       "KeyPollWatch"))
    DSVR_WARN("Input: GetKeyboardState watch unavailable; thread-queue polling stays unobserved");
  const HMODULE dinput = GetModuleHandleA("dinput8.dll");
  if (!dinput ||
      !iat::InstallIat(dinput, "dinput8.dll", "DirectInput8Create",
                       g_dicreate_iat,
                       reinterpret_cast<void*>(&LogDirectInput8Create),
                       "KeyPollWatch"))
    DSVR_WARN("Input: DirectInput8Create watch unavailable; device creation stays unobserved");
  return true;
}

void XInputBridgeUninstall() {
  std::lock_guard<std::mutex> install_lock(HookInstallMutex());
  RemoveXInputIat(g_state_iat, reinterpret_cast<void*>(&XInputIatDetour));
  RemoveXInputIat(g_caps_iat, reinterpret_cast<void*>(&XInputCapsIatDetour));
  RemoveXInputIat(g_setstate_iat, reinterpret_cast<void*>(&XInputSetStateIatDetour));
  iat::RemoveIat(g_procaddress_iat,
                 reinterpret_cast<void*>(&XInputGetProcAddressDetour),
                 "XInputProcAddress");
  iat::RemoveIat(g_askeys_iat, reinterpret_cast<void*>(&LogGetAsyncKeyState),
                 "KeyPollWatch");
  iat::RemoveIat(g_kbstate_iat, reinterpret_cast<void*>(&LogGetKeyboardState),
                 "KeyPollWatch");
  iat::RemoveIat(g_dicreate_iat,
                 reinterpret_cast<void*>(&LogDirectInput8Create),
                 "KeyPollWatch");
  g_inline_enabled.store(false, std::memory_order_release);
  g_caps_inline_enabled.store(false, std::memory_order_release);
  g_setstate_inline_enabled.store(false, std::memory_order_release);
  const void* targets[] = {g_target, g_caps_target, g_setstate_target};
  const bool owned[] = {static_cast<bool>(g_original.load(std::memory_order_acquire)),
                        static_cast<bool>(g_caps_original.load(std::memory_order_acquire)),
                        static_cast<bool>(g_setstate_original.load(std::memory_order_acquire))};
  for (std::size_t i = 0; i < 3; ++i) {
    if (!targets[i] || !owned[i]) continue;
    const auto status = MH_DisableHook(const_cast<void*>(targets[i]));
    if (status != MH_OK && status != MH_ERROR_DISABLED)
      DSVR_WARN("XInput: hook disable failed (%s); retained callback is pass-through",
                MH_StatusToString(status));
  }
  // A late/in-flight callback can still call its trampoline after disabling.
  // Keep our owned hooks and pinned modules alive; re-enable them on reinstall.
}

}  // namespace dsvr
