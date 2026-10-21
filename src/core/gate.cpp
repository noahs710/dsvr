#include "gate.h"

#include <windows.h>

#include <string>

#include "../common/logging.h"
#include "../common/memory.h"
#include "../common/version.h"

namespace dsvr {

bool GateCheckPinnedBuild() {
  std::wstring exe = CurrentExePath();
  DSVR_LOG("Gate: exe=%ls", exe.c_str());

  // 1. File size (cheap pre-check before hashing 50 MB).
  LARGE_INTEGER size{};
  {
    HANDLE f =
        CreateFileW(exe.c_str(), GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                    0, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
      DSVR_ERROR("Gate: cannot open exe (%lu); refusing", GetLastError());
      return false;
    }
    if (!GetFileSizeEx(f, &size)) {
      DSVR_ERROR("Gate: cannot stat exe; refusing");
      CloseHandle(f);
      return false;
    }
    CloseHandle(f);
  }
  DSVR_LOG("Gate: file size=%lld (expected %llu)",
           (long long)size.QuadPart, kExpectedFileSize);
  if ((unsigned long long)size.QuadPart != kExpectedFileSize) {
    DSVR_ERROR("Gate: size mismatch; refusing (see MODDING_PLAN for supported build)");
    return false;
  }

  // 2. PE headers of the running image.
  std::uintptr_t base = ModuleBase(nullptr);
  IMAGE_DOS_HEADER dos{};
  IMAGE_NT_HEADERS64 nt{};
  if (!GuardedRead(base, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE) {
    DSVR_ERROR("Gate: bad DOS header; refusing");
    return false;
  }
  if (!GuardedRead(base + (std::uintptr_t)dos.e_lfanew, nt) ||
      nt.Signature != IMAGE_NT_SIGNATURE) {
    DSVR_ERROR("Gate: bad NT headers; refusing");
    return false;
  }
  DSVR_LOG("Gate: machine=0x%x timestamp=0x%lx sizeofimage=0x%lx",
           nt.FileHeader.Machine, (unsigned long)nt.FileHeader.TimeDateStamp,
           (unsigned long)nt.OptionalHeader.SizeOfImage);
  if (nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
      nt.FileHeader.TimeDateStamp != kExpectedTimeDateStamp ||
      nt.OptionalHeader.SizeOfImage != kExpectedSizeOfImage) {
    DSVR_ERROR("Gate: PE mismatch (want AMD64/0x%lx/0x%lx); refusing",
               kExpectedTimeDateStamp, kExpectedSizeOfImage);
    return false;
  }

  // 3. Full SHA-256 (authoritative; ~50 MB, runs once at startup).
  std::string sha;
  if (!FileSha256(exe.c_str(), sha)) {
    DSVR_ERROR("Gate: hash failed; refusing");
    return false;
  }
  DSVR_LOG("Gate: sha256=%s", sha.c_str());
  if (sha != kExpectedSha256) {
    DSVR_ERROR("Gate: SHA mismatch; refusing (unsupported game version)");
    return false;
  }

  DSVR_LOG("Gate: pinned build verified; continuing");
  return true;
}

}  // namespace dsvr
