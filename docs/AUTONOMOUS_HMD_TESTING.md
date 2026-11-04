# OpenXR Simulator testing on Windows

The supported simulator is Elliott Tate's unmodified OpenXR-Simulator repository at commit 692a66de570189b29c684f48c6ef306a07d5764f. The former DSVR-specific controller-state extension and standalone DSVR-HmdLab driver are not upstream interfaces and are retired from the standard test path.

## Setup

From the repository root in PowerShell 7:

    pwsh -NoProfile -File tools/hmd/setup_openxr_simulator.ps1

Setup pins the clean upstream checkout, creates an isolated Python environment, installs the official MCP server, configures and builds the runtime plus real UI automation probes, and records artifact hashes in build/research/openxr-simulator-upstream-build/upstream-lock.json. It uses XR_RUNTIME_JSON per process for test apps; it does not change the machine-wide OpenXR registration.

The MCP server is registered in this project's Empryo config as openxr-simulator. It uses the isolated Python and build/openxr-session-data IPC directory. The runtime and MCP server must use the same SIMXR_DATA_DIR; the test runner assigns a fresh directory per run. The connected server exposes 36 upstream tools for screenshots, diagnostics, headset pose/FOV/IPD, UI ray hover/click/hold/scroll, frame timing and flicker capture.

The simulator supports D3D11, D3D12, Vulkan and OpenGL, but no hand tracking or haptics. Controller input is driven through the documented UI-targeting actions or the simulator's own keyboard controls. The simulator's set_controller_state extension from the retired DSVR fork is not used.

## Automated verification

    pwsh -NoProfile -File tools/run_hmd_tests.ps1 -SelfTest
    pwsh -NoProfile -File tools/run_hmd_tests.ps1

The full run executes the upstream CTest suite, including real OpenXR applications driven over MCP, then builds/tests DSVR in the selected configuration. Each run writes an immutable receipt and logs under build/verification/upstream-simulator-*; captures and per-case results remain under the simulator build's ui-evidence directory. No mouse or keyboard action by a human is required by the CTest fixtures.

For the upstream C++/noninteractive suite without its live GPU probes:

    ctest --test-dir build/research/openxr-simulator-upstream-build -C Release -E "^(ui_e2e_|composition_order_)" --output-on-failure --parallel

For one end-to-end interaction test:

    ctest --test-dir build/research/openxr-simulator-upstream-build -C Release -R "^ui_e2e_d3d11$" --output-on-failure

The e2e tests render a real OpenXR app, capture the mirror, hover/click/hold/scroll using MCP controller rays, and verify app-side activation, press/release edges and screen-space ray error. They cover both hands, eyes, display layouts, graphics backends, scene and quad UI, asymmetric FOV, stale captures, idempotent retries and cleanup. Reports must retain the probe exit code; a visible click pass is not a test pass if app teardown fails.

## Current machine result

The upstream checkout and MCP setup succeeded. The 23 upstream unit tests pass. In the full 42-test suite, 17 live UI cases wrote passing interaction results, but the subprocess tests were marked failed because probe shutdown raises 0xc0000409 in AMD amdxc64.dll/amdxx64.dll; WER names driver offsets, not the simulator. The failure reproduces across D3D11, D3D12, OpenGL, Vulkan and composition probes after the UI assertions pass. It is preserved as a blocker, not waived. No system runtime registration, DSR install or saves were changed by the upstream test run.

A passing UI assertion with a failing process exit is not a passing suite. Keep the teardown crash visible until it is resolved or upstream provides a verified fix. Headset optics, physical comfort, natural arm alignment and physical melee still require hardware/operator evidence; the simulator cannot qualify them.
