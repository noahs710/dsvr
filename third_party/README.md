# third_party

Dependencies fetched by CMake FetchContent:

- MinHook v1.3.4 — native hooks; BSD-style license, including its bundled
  disassembler notices. https://github.com/TsudaKageyu/minhook
- Khronos OpenXR-SDK release-1.1.49 — headers and statically linked loader;
  Apache-2.0 or MIT as specified by its license.
  https://github.com/KhronosGroup/OpenXR-SDK
- Dear ImGui v1.91.9b (`f5befd2d29e66809cd1110a152e375a7f1981f06`) � native
  settings/calibration overlay with the DX11 backend; MIT.
  https://github.com/ocornut/imgui
- JsonCpp — bundled with that OpenXR SDK; its license is included separately.
- Optional ReShade SDK v6.6.2 / API 18, commit
  `7d13ead8f630d045b9e7e2ac2ce18b2e7e5b25c3` — header-only add-on interface,
  BSD-3-Clause or MIT. https://github.com/crosire/reshade
  No ReShade runtime is built or distributed. The SDK notice ships with the
  optional add-on package.

Distribution notices are in `licenses/` in the test package. Tests use the
project's own harness; Catch2 is not a runtime dependency. Windows graphics
libraries are system components. No game files or FPDSR/ERVR binaries ship.

FPDSR v1.5 and ERVR 0.4.0 informed static research and design; this is not
an endorsement by their authors. DSVR's original code is MIT (see LICENSE).
AI-assisted development: OpenCode with OpenAI GPT-6.1 Sol contributed to this
candidate's preparation; it has not been certified by human headset review.
