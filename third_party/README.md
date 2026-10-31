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

- Required external runtime dependency for high-rate DSR (DSVR still starts at the native rate without it): [DS1-FPS-Unlock-PTDE-Remastered](https://github.com/zofrea1/DS1-FPS-Unlock-PTDE-Remastered), DSR `v1.9.0-REMASTERED-DX11`; MIT; official release ZIP SHA-256 `37797dd85d49ce440bb693d2cf27ff2993c0d4327f78f10ebe4c7e1751cefbba`. Not bundled or copied into DSVR releases. Its variable-frame-time simulation, frame-pacer correction and frame-dependent physics fixes own high-FPS gameplay behavior. Install its `dinput8.dll` + `DSR-FPS-Unlock.ini` first, then run DSVR-Setup and accept chain-loading so DSVR forwards DirectInput8Create through `dinput8-chain.dll`.
  DSVR's loader updates only `[Framerate]` `FPSUnlock`, `VariableFrameTime`, and `MaxFPS` before loading the dependency. VR uses the last HMD-reported refresh in DSVR's sidecar; when none is known, the loader disables `FPSUnlock` instead of selecting a preset. With VR disabled, it passes the current monitor refresh. DSR-FPS-Unlock alone applies `MaxFPS` and `VariableFrameTime`; DSVR never caps or uncaps the game. The dependency reads its INI once at startup, so refresh changes apply next launch. Other dependency INI settings remain intact. Upstream documents high-rate movement/physics as close to native, not bit-exact on fixed-frame-grid edge cases such as slopes and lips; the dependency's own native/headset qualification remains required. Upstream warns that official online play is untested and could risk a ban; play offline while installed.

Distribution notices are in `licenses/` in the test package. Tests use the
project's own harness; Catch2 is not a runtime dependency. Windows graphics
libraries are system components. No game files or FPDSR/ERVR binaries ship.

FPDSR v1.5 and ERVR 0.4.0 informed static research and design; this is not
an endorsement by their authors. DSVR's original code is MIT (see LICENSE).
AI-assisted development: OpenCode with OpenAI GPT-6.1 Sol contributed to this
candidate's preparation; it has not been certified by human headset review.
