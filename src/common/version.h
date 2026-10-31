#pragma once

#include <cstring>

// Game builds this VR mod supports. Anything else is refused with a clear
// message naming the build the player actually has. Facts about a build (not
// copied code). A new Steam patch changes the hash, the PE timestamp and the
// image size together, so every identity fact is checked as one tuple: an
// offset or pattern that was recovered for one build cannot be assumed to hold
// for another, and a partial match is what turns a clean refusal into a
// memory-safety bug.
//
// Adding a row is a porting task, not a relaxation: the row is only valid once
// the pinned evidence for that build (docs/ARM_IK_EVIDENCE.md,
// docs/NATIVE_STEREO_CONTRACT.md, docs/NATIVE_MELEE_CONTRACT.md) is recovered
// and the offline suites pass against it.

namespace dsvr {

struct SupportedBuild {
  unsigned long long file_size;
  unsigned long time_date_stamp;
  unsigned long size_of_image;
  const char* sha256;
  const char* label;  // what the player sees in Setup, logs and the README
};

// Steam 570940, App ver 1.03.1, Steam build 10943698:
// DarkSoulsRemastered.exe, 50,286,344 bytes, x64, D3D11.
inline constexpr SupportedBuild kSupportedBuilds[] = {
    {50286344ull, 0x6344CA56, 0x319B000,
     "a45aaa36dd2f6cc151670a639ea5547043cf38ea79ff4178b963c6ed71f98d7b",
     "App 1.03.1 / build 10943698"},
};
inline constexpr unsigned kSupportedBuildCount =
    sizeof(kSupportedBuilds) / sizeof(kSupportedBuilds[0]);

// The current row the mod was built and tested against.
inline constexpr SupportedBuild kPinnedBuild = kSupportedBuilds[0];

inline constexpr char kExpectedSha256[] =
    "a45aaa36dd2f6cc151670a639ea5547043cf38ea79ff4178b963c6ed71f98d7b";
inline constexpr unsigned long kExpectedSizeOfImage = 0x319B000;  // 52015104
inline constexpr unsigned long kExpectedTimeDateStamp = 0x6344CA56;
inline constexpr unsigned long long kExpectedFileSize = 50286344ull;

// Index into kSupportedBuilds when every identity fact matches one supported
// build, or -1 when the exe is a build DSVR has never been qualified against.
// Matching is all-or-nothing on purpose: a size that happens to survive a
// patch says nothing about the bytes inside the image.
inline long FindSupportedBuild(unsigned long long file_size,
                               unsigned long time_date_stamp,
                               unsigned long size_of_image,
                               const char* sha256) {
  for (unsigned i = 0; i < kSupportedBuildCount; ++i) {
    const SupportedBuild& b = kSupportedBuilds[i];
    if (b.file_size == file_size && b.time_date_stamp == time_date_stamp &&
        b.size_of_image == size_of_image && sha256 &&
        std::strcmp(b.sha256, sha256) == 0)
      return static_cast<long>(i);
  }
  return -1;
}

inline constexpr char kModVersion[] = "0.16.0-render-host-preview";
inline constexpr char kLogFileName[] = "DSVR.log";
inline constexpr char kCoreDllName[] = "DSVR.dll";
inline constexpr char kConfigFileName[] = "DSVR.toml";

}  // namespace dsvr
