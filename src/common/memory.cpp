#include "memory.h"

#include <windows.h>

#include <bcrypt.h>

#include <array>
#include <cctype>

namespace dsvr {
namespace {

bool IsReadable(std::uintptr_t address, std::size_t size) {
  if (address == 0 || size == 0 || size > ~std::uintptr_t(0)-address) return false;
  if (GuardedReadScope::Contains(address,address+size)) return true;
  MEMORY_BASIC_INFORMATION mbi{};
  if (VirtualQuery((LPCVOID)address, &mbi, sizeof(mbi)) == 0) return false;
  if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD)) return false;
  const DWORD p = mbi.Protect & 0xff;
  if (p != PAGE_READONLY && p != PAGE_READWRITE && p != PAGE_WRITECOPY &&
      p != PAGE_EXECUTE_READ && p != PAGE_EXECUTE_READWRITE &&
      p != PAGE_EXECUTE_WRITECOPY) return false;
  std::uintptr_t end = address + size;
  std::uintptr_t region_end =
      (std::uintptr_t)mbi.BaseAddress + mbi.RegionSize;
  const bool writable=p==PAGE_READWRITE || p==PAGE_WRITECOPY ||
      p==PAGE_EXECUTE_READWRITE || p==PAGE_EXECUTE_WRITECOPY;
  GuardedReadScope::Remember((std::uintptr_t)mbi.BaseAddress,region_end,writable);
  if (end > region_end) {
    // Spans regions: check the tail recursively (bounded, one page step).
    return IsReadable(region_end, end - region_end);
  }
  return true;
}

struct PatternByte {
  bool wildcard = false;
  unsigned char value = 0;
};

bool ParsePattern(const char* text, std::vector<PatternByte>& out) {
  out.clear();
  const char* p = text;
  while (*p) {
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) break;
    if (*p == '?') {
      p++;
      if (*p == '?') p++;  // "??" also means one wildcard byte
      out.push_back({true, 0});
    } else {
      if (!isxdigit((unsigned char)p[0]) || !isxdigit((unsigned char)p[1]))
        return false;
      char hex[3] = {p[0], p[1], 0};
      out.push_back({false, (unsigned char)strtoul(hex, nullptr, 16)});
      p += 2;
    }
  }
  return !out.empty();
}

}  // namespace

std::uintptr_t ModuleBase(const wchar_t* module_name_or_null) {
  HMODULE h = GetModuleHandleW(module_name_or_null);
  return (std::uintptr_t)h;
}

std::size_t ModuleSize(std::uintptr_t base) {
  if (!IsReadable(base, sizeof(IMAGE_DOS_HEADER))) return 0;
  auto* dos = (IMAGE_DOS_HEADER*)base;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
  if (!IsReadable(base + (std::uintptr_t)dos->e_lfanew, sizeof(IMAGE_NT_HEADERS64)))
    return 0;
  auto* nt = (IMAGE_NT_HEADERS64*)(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
  return nt->OptionalHeader.SizeOfImage;
}

bool GuardedReadBytes(std::uintptr_t address, void* out, std::size_t size) {
  if (!out || size == 0 || !IsReadable(address, size)) return false;
  __try {
    memcpy(out, (const void*)address, size);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    GuardedReadScope::Invalidate();
    return false;
  }
}

bool GuardedWriteBytes(std::uintptr_t address, const void* data,
                       std::size_t size) {
  if (!data || size == 0 || address == 0 || size > ~std::uintptr_t(0)-address) return false;
  // Only a permission hint; copy live bytes under SEH even on a cache hit.
  if (GuardedReadScope::Contains(address,address+size,true)) {
    __try {memcpy(reinterpret_cast<void*>(address),data,size);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {GuardedReadScope::Invalidate();return false;}
  }
  MEMORY_BASIC_INFORMATION mbi{};
  if (!VirtualQuery(reinterpret_cast<void*>(address),&mbi,sizeof(mbi)) ||
      mbi.State!=MEM_COMMIT || (mbi.Protect&PAGE_GUARD) ||
      address+size>reinterpret_cast<std::uintptr_t>(mbi.BaseAddress)+mbi.RegionSize) return false;
  const DWORD protection=mbi.Protect & 0xff;
  if(protection==PAGE_NOACCESS || !protection)return false;
  const bool writable=protection==PAGE_READWRITE || protection==PAGE_WRITECOPY ||
      protection==PAGE_EXECUTE_READWRITE || protection==PAGE_EXECUTE_WRITECOPY;
  if(writable)GuardedReadScope::Remember(reinterpret_cast<std::uintptr_t>(mbi.BaseAddress),
      reinterpret_cast<std::uintptr_t>(mbi.BaseAddress)+mbi.RegionSize,true);
  DWORD old=0;
  if(!writable) {
    GuardedReadScope::Invalidate();
    const bool executable=protection==PAGE_EXECUTE || protection==PAGE_EXECUTE_READ;
    if(!VirtualProtect(reinterpret_cast<void*>(address),size,
        executable?PAGE_EXECUTE_READWRITE:PAGE_READWRITE,&old))return false;
  }
  bool ok=false;
  __try {memcpy(reinterpret_cast<void*>(address),data,size);ok=true;}
  __except(EXCEPTION_EXECUTE_HANDLER) {GuardedReadScope::Invalidate();ok=false;}
  if(!writable) {
    DWORD ignored=0;
    if(!VirtualProtect(reinterpret_cast<void*>(address),size,old,&ignored))ok=false;
    GuardedReadScope::Invalidate();
  }
  return ok;
}

std::optional<std::uintptr_t> FollowChain(std::uintptr_t base,
                                           const std::uintptr_t* offsets,
                                           std::size_t count) {
  std::uintptr_t cur = base;
  for (std::size_t i = 0; i < count; i++) {
    std::uintptr_t next = 0;
    if (!GuardedRead(cur + offsets[i], next) || next == 0) return std::nullopt;
    cur = next;
  }
  return cur;
}

std::vector<std::uintptr_t> PatternScan(std::uintptr_t module_base,
                                        const char* pattern) {
  std::vector<std::uintptr_t> hits;
  std::vector<PatternByte> pat;
  if (!ParsePattern(pattern, pat) || pat.empty()) return hits;
  if (!IsReadable(module_base, sizeof(IMAGE_DOS_HEADER))) return hits;
  auto* dos = (IMAGE_DOS_HEADER*)module_base;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return hits;
  auto* nt = (IMAGE_NT_HEADERS64*)(module_base + dos->e_lfanew);
  if (!IsReadable((std::uintptr_t)nt, sizeof(IMAGE_NT_HEADERS64))) return hits;
  if (nt->Signature != IMAGE_NT_SIGNATURE) return hits;
  auto* sec = IMAGE_FIRST_SECTION(nt);
  for (unsigned s = 0; s < nt->FileHeader.NumberOfSections; s++) {
    if (!(sec[s].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
    std::uintptr_t start = module_base + sec[s].VirtualAddress;
    std::size_t size = sec[s].Misc.VirtualSize;
    if (size < pat.size()) continue;
    for (std::size_t i = 0; i + pat.size() <= size; i++) {
      bool match = true;
      for (std::size_t j = 0; j < pat.size(); j++) {
        if (pat[j].wildcard) continue;
        unsigned char b = 0;
        if (!GuardedRead(start + i + j, b) || b != pat[j].value) {
          match = false;
          break;
        }
      }
      if (match) hits.push_back(start + i - module_base);  // RVA
      if (hits.size() >= 64) return hits;  // cap: ambiguous pattern
    }
  }
  return hits;
}

bool BytesMatch(std::uintptr_t address, const void* expected, std::size_t size) {
  if (!expected || size == 0 || !IsReadable(address, size)) return false;
  __try {
    return memcmp((const void*)address, expected, size) == 0;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

bool FileSha256(const wchar_t* path, std::string& out_hex) {
  out_hex.clear();
  HANDLE f = CreateFileW(path, GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         OPEN_EXISTING, 0, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  BCRYPT_ALG_HANDLE alg{};
  BCRYPT_HASH_HANDLE hash{};
  bool ok = false;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >=
          0 &&
      BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) >= 0) {
    std::array<unsigned char, 32768> buf{};
    DWORD n = 0;
    bool valid = true;
    for (;;) {
      if (!ReadFile(f, buf.data(), (DWORD)buf.size(), &n, nullptr)) {
        valid = false;
        break;
      }
      if (n == 0) break;
      if (BCryptHashData(hash, buf.data(), n, 0) < 0) {
        valid = false;
        break;
      }
    }
    unsigned char digest[32]{};
    if (valid && BCryptFinishHash(hash, digest, 32, 0) >= 0) {
      char hex[65]{};
      for (int i = 0; i < 32; i++) sprintf_s(hex + i * 2, 3, "%02x", digest[i]);
      out_hex = hex;
      ok = true;
    }
  }
  if (hash) BCryptDestroyHash(hash);
  if (alg) BCryptCloseAlgorithmProvider(alg, 0);
  CloseHandle(f);
  return ok;
}

std::wstring CurrentExePath() {
  wchar_t path[32768]{};
  DWORD n = GetModuleFileNameW(nullptr, path, 32767);
  if (n == 0 || n >= 32767) return L"";
  return std::wstring(path, n);
}

}  // namespace dsvr
