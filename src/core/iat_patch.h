#pragma once

// Game-local IAT patching: deterministic import-table redirection with no
// trampoline allocation. MinHook-style prologue hooks near system DLLs fail
// intermittently in this process (MH_ERROR_MEMORY_ALLOC lottery), while an
// IAT slot exchange cannot fail for space reasons. Mirrors the discipline of
// the XInput bridge: bounded image reads, unique-slot requirement, cache-line
// alignment check, protection save/restore, native LOCK CMPXCHG, module
// pinning, and owner-checked removal.
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>

#include "../common/logging.h"

namespace dsvr {
namespace iat {

extern "C" std::uintptr_t DsvrIatCompareExchange(void* slot,
                                                std::uintptr_t expected,
                                                std::uintptr_t desired);

struct IatPatch {
  void* slot = nullptr;
  HMODULE image = nullptr;
  std::uintptr_t original = 0;
  DWORD restore_protection = 0;
  bool protection_pending = false;
  std::atomic<bool> enabled{false};
};

inline std::size_t ModuleImageSize(std::uintptr_t base) {
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
      base + static_cast<std::uintptr_t>(dos->e_lfanew));
  if (nt->Signature != IMAGE_NT_SIGNATURE ||
      nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
      nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    return 0;
  return nt->OptionalHeader.SizeOfImage;
}

template <typename T>
inline bool ReadImage(std::uintptr_t base, std::size_t size,
                      std::uint64_t offset, T& out) {
  if (offset > size || sizeof(T) > size - static_cast<std::size_t>(offset))
    return false;
  std::memcpy(&out, reinterpret_cast<const void*>(base +
              static_cast<std::uintptr_t>(offset)), sizeof(T));
  return true;
}

inline bool ImageString(std::uintptr_t base, std::size_t size,
                        std::uint64_t offset, char* out, std::size_t capacity) {
  for (std::size_t i = 0; i < capacity; ++i) {
    if (offset + i >= size) return false;
    out[i] = *reinterpret_cast<const char*>(base +
             static_cast<std::uintptr_t>(offset + i));
    if (!out[i]) return true;
  }
  return false;
}

// Unique FirstThunk slot in the game image currently bound to
// provider:import_name, or nullptr (missing, duplicated, or already owned).
inline void* FindGameImport(HMODULE image, HMODULE provider,
                            const char* provider_dll, const char* import_name) {
  const auto base = reinterpret_cast<std::uintptr_t>(image);
  const std::size_t size = ModuleImageSize(base);
  IMAGE_DOS_HEADER dos{};
  IMAGE_NT_HEADERS64 nt{};
  if (size == 0 || !ReadImage(base, size, 0, dos) || dos.e_lfanew < 0 ||
      !ReadImage(base, size, dos.e_lfanew, nt) ||
      nt.OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_IMPORT)
    return nullptr;
  const auto dir = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (!dir.VirtualAddress || dir.VirtualAddress > size ||
      dir.Size > size - dir.VirtualAddress)
    return nullptr;
  void* target = reinterpret_cast<void*>(
      GetProcAddress(provider, import_name));
  if (!target) return nullptr;
  void* found = nullptr;
  for (std::size_t off = 0;
       off + sizeof(IMAGE_IMPORT_DESCRIPTOR) <= dir.Size;
       off += sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
    IMAGE_IMPORT_DESCRIPTOR descriptor{};
    if (!ReadImage(base, size, std::uint64_t(dir.VirtualAddress) + off,
                   descriptor))
      return nullptr;
    if (!descriptor.Name)
      return !descriptor.FirstThunk && !descriptor.OriginalFirstThunk ? found
                                                                     : nullptr;
    char dll[64]{};
    if (!ImageString(base, size, descriptor.Name, dll, sizeof(dll)))
      return nullptr;
    if (_stricmp(dll, provider_dll) != 0) continue;
    if (GetModuleHandleA(dll) != provider) continue;
    if (!descriptor.OriginalFirstThunk || !descriptor.FirstThunk)
      return nullptr;
    bool terminated = false;
    for (std::uint64_t i = 0; i < 4096; ++i) {
      IMAGE_THUNK_DATA64 lookup{};
      const auto delta = i * sizeof(lookup);
      if (!ReadImage(base, size,
                     std::uint64_t(descriptor.OriginalFirstThunk) + delta,
                     lookup))
        return nullptr;
      if (!lookup.u1.AddressOfData) { terminated = true; break; }
      void* imported = nullptr;
      if (IMAGE_SNAP_BY_ORDINAL64(lookup.u1.Ordinal)) {
        imported = reinterpret_cast<void*>(GetProcAddress(
            provider, MAKEINTRESOURCEA(
                          static_cast<WORD>(IMAGE_ORDINAL64(lookup.u1.Ordinal)))));
      } else {
        char name[64]{};
        if (lookup.u1.AddressOfData > size ||
            !ImageString(base, size,
                         lookup.u1.AddressOfData +
                             offsetof(IMAGE_IMPORT_BY_NAME, Name),
                         name, sizeof(name)))
          return nullptr;
        imported = reinterpret_cast<void*>(GetProcAddress(provider, name));
      }
      if (imported != target) continue;
      std::uintptr_t current = 0;
      const auto offset = std::uint64_t(descriptor.FirstThunk) + delta;
      if (found || !ReadImage(base, size, offset, current) ||
          current != reinterpret_cast<std::uintptr_t>(target))
        return nullptr;
      found = reinterpret_cast<void*>(base + static_cast<std::uintptr_t>(offset));
    }
    if (!terminated) return nullptr;
  }
  return found;
}

inline bool PinIatLifetime(void* provider_export, void* detour) {
  HMODULE pinned = nullptr;
  const DWORD flags =
      GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN;
  return GetModuleHandleExW(flags,
                            reinterpret_cast<LPCWSTR>(provider_export),
                            &pinned) &&
         GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(detour), &pinned);
}

inline bool ExchangeIatSlot(IatPatch& patch, void* expected, void* desired,
                            bool& exchanged, const char* what) {
  exchanged = false;
  MEMORY_BASIC_INFORMATION info{};
  const auto address = reinterpret_cast<std::uintptr_t>(patch.slot);
  if (!patch.slot || (address & 63) > 64 - sizeof(void*) ||
      !VirtualQuery(patch.slot, &info, sizeof(info)) ||
      info.AllocationBase != patch.image || info.State != MEM_COMMIT ||
      (info.Protect & (PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE)) ||
      info.Protect == PAGE_NOACCESS ||
      address + sizeof(void*) > reinterpret_cast<std::uintptr_t>(info.BaseAddress) +
                                  info.RegionSize)
    return false;
  DWORD old = 0;
  const DWORD writable =
      (info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                       PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))
          ? PAGE_EXECUTE_READWRITE
          : PAGE_READWRITE;
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
  if (patch.protection_pending)
    DSVR_ERROR("%s: IAT page protection restore failed", what);
  return !patch.protection_pending;
}

inline void RemoveIat(IatPatch& patch, void* detour, const char* what) {
  patch.enabled.store(false, std::memory_order_release);
  if (!patch.slot) return;
  bool exchanged = false;
  if (!ExchangeIatSlot(patch, detour,
                       reinterpret_cast<void*>(patch.original), exchanged,
                       what)) {
    DSVR_ERROR("%s: IAT cleanup incomplete; disabled, retaining restore state",
               what);
    return;
  }
  if (!exchanged)
    DSVR_WARN("%s: IAT owner changed; leaving foreign entry untouched", what);
  patch.slot = nullptr;
  patch.image = nullptr;
}

// Patch the game image's unique provider:import_name slot to detour.
// Refuses (loudly) when the import is missing, duplicated, or not currently
// bound to the provider's export.
inline bool InstallIat(HMODULE provider, const char* provider_dll,
                       const char* import_name, IatPatch& patch, void* detour,
                       const char* what) {
  HMODULE image = GetModuleHandleW(nullptr);
  void* target = reinterpret_cast<void*>(
      GetProcAddress(provider, import_name));
  if (!target) {
    DSVR_ERROR("%s: provider has no export %s", what, import_name);
    return false;
  }
  void* slot = FindGameImport(image, provider, provider_dll, import_name);
  if (!slot ||
      (reinterpret_cast<std::uintptr_t>(slot) & 63) > 64 - sizeof(void*)) {
    DSVR_ERROR("%s: no unique owned game %s import; IAT patch refused", what,
               import_name);
    return false;
  }
  if (patch.original && patch.original != reinterpret_cast<std::uintptr_t>(target))
    return false;
  if (!PinIatLifetime(target, detour)) return false;
  patch.original = reinterpret_cast<std::uintptr_t>(target);
  patch.image = image;
  patch.slot = slot;
  bool exchanged = false;
  if (!ExchangeIatSlot(patch, target, detour, exchanged, what) || !exchanged) {
    RemoveIat(patch, detour, what);
    return false;
  }
  patch.enabled.store(true, std::memory_order_release);
  DSVR_LOG("%s: game-local IAT patch ready for %s", what, import_name);
  return true;
}

}  // namespace iat
}  // namespace dsvr
