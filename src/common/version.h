#pragma once

// Pinned game build this VR mod supports. Anything else is refused with a clear
// log message. Facts about the build (not copied code):
// - Steam 570940, App ver 1.03.1, Steam build 10943698
// - DarkSoulsRemastered.exe, 50,286,344 bytes, x64, D3D11

namespace dsvr {

inline constexpr char kExpectedSha256[] =
    "a45aaa36dd2f6cc151670a639ea5547043cf38ea79ff4178b963c6ed71f98d7b";
inline constexpr unsigned long kExpectedSizeOfImage = 0x319B000;  // 52015104
inline constexpr unsigned long kExpectedTimeDateStamp = 0x6344CA56;
inline constexpr unsigned long long kExpectedFileSize = 50286344ull;

inline constexpr char kModVersion[] = "0.16.0-render-host-preview";
inline constexpr char kLogFileName[] = "DSVR.log";
inline constexpr char kCoreDllName[] = "DSVR.dll";
inline constexpr char kConfigFileName[] = "DSVR.toml";

}  // namespace dsvr
