#include "gate.h"

#include <windows.h>

#include <string>

#include "../common/logging.h"
#include "../common/memory.h"
#include "../common/version.h"

namespace dsvr {

namespace {

// One line naming every build DSVR can run against, so a refusal on a patched
// exe tells the player what they have and what is supported in the same place.
std::string SupportedBuildList() {
  std::string list;
  for (unsigned i = 0; i < kSupportedBuildCount; ++i) {
    if (i) list += "; ";
    list += kSupportedBuilds[i].label;
    list += " (";
    list += std::to_string(kSupportedBuilds[i].file_size);
    list += " bytes)";
  }
  return list;
}

bool SizeIsSupported(unsigned long long size) {
  for (unsigned i = 0; i < kSupportedBuildCount; ++i)
    if (kSupportedBuilds[i].file_size == size) return true;
  return false;
}

}  // namespace

bool GateCheckPinnedBuild() {
  std::wstring exe = CurrentExePath();
  DSVR_LOG("Gate: exe=%ls", exe.c_str());
  DSVR_LOG("Gate: supported builds: %s", SupportedBuildList().c_str());

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
  DSVR_LOG("Gate: file size=%lld (supported: %s)",
           (long long)size.QuadPart, SupportedBuildList().c_str());
  if (!SizeIsSupported(static_cast<unsigned long long>(size.QuadPart))) {
    DSVR_ERROR("Gate: size mismatch; refusing (this build is not qualified; "
               "supported: %s)", SupportedBuildList().c_str());
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
  if (nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) {
    DSVR_ERROR("Gate: not an AMD64 image; refusing");
    return false;
  }

  // 3. Full SHA-256 (authoritative; ~50 MB, runs once at startup).
  std::string sha;
  if (!FileSha256(exe.c_str(), sha)) {
    DSVR_ERROR("Gate: hash failed; refusing");
    return false;
  }
  DSVR_LOG("Gate: sha256=%s", sha.c_str());
  const long match = FindSupportedBuild(
      static_cast<unsigned long long>(size.QuadPart),
      nt.FileHeader.TimeDateStamp, nt.OptionalHeader.SizeOfImage, sha.c_str());
  if (match < 0) {
    DSVR_ERROR(
        "Gate: unsupported game build (size=%lld timestamp=0x%lx "
        "sizeofimage=0x%lx sha256=%s); refusing. Supported: %s. A patch "
        "changes the image layout, so nothing is assumed about it.",
        (long long)size.QuadPart, (unsigned long)nt.FileHeader.TimeDateStamp,
        (unsigned long)nt.OptionalHeader.SizeOfImage, sha.c_str(),
        SupportedBuildList().c_str());
    return false;
  }

  DSVR_LOG("Gate: supported build verified (%s); continuing",
           kSupportedBuilds[static_cast<unsigned>(match)].label);
  return true;
}

}  // namespace dsvr
